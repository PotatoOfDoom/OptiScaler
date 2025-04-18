#pragma once

#include <pch.h>
#include <d3dcompiler.h>

struct RcasConstants
{
    float Sharpness;

    // Motion Vector Stuff
    bool DynamicSharpenEnabled;
    bool DisplaySizeMV;
    bool Debug;

    float MotionSharpness;
    float MotionTextureScale;
    float MvScaleX;
    float MvScaleY;
    float Threshold;
    float ScaleLimit;

    int RenderWidth;
    int RenderHeight;
    int DisplayWidth;
    int DisplayHeight;
};

static std::string rcasCode = R"(
// Based on this Reshade shader
// https://github.com/RdenBlaauwen/RCAS-for-ReShade

cbuffer Params : register(b0)
{
    float Sharpness;
    float Contrast;

    // Motion Vector Stuff
    int DynamicSharpenEnabled;
    int DisplaySizeMV;
    int Debug;
    
    float MotionSharpness;
    float MotionTextureScale;
    float MvScaleX;
    float MvScaleY;
    float Threshold;
    float ScaleLimit;
    int DisplayWidth;
    int DisplayHeight;
};

Texture2D<float3> Source : register(t0);
Texture2D<float2> Motion : register(t1);
RWTexture2D<float3> Dest : register(u0);

float getRCASLuma(float3 rgb)
{
    return dot(rgb, float3(0.5, 1.0, 0.5));
}

[numthreads(32, 32, 1)]
void CSMain(uint3 DTid : SV_DispatchThreadID)
{
    float setSharpness = Sharpness;
  
    if (DynamicSharpenEnabled > 0)
    {
        float2 mv;
        float motion;
        float add = 0.0f;

        if (DisplaySizeMV > 0)
            mv = Motion.Load(int3(DTid.x, DTid.y, 0)).rg;
        else
            mv = Motion.Load(int3(DTid.x * MotionTextureScale, DTid.y * MotionTextureScale, 0)).rg;

        motion = max(abs(mv.r * MvScaleX), abs(mv.g * MvScaleY));

        if (motion > Threshold)
            add = (motion / (ScaleLimit - Threshold)) * MotionSharpness;
    
        if ((add > MotionSharpness && MotionSharpness > 0.0f) || (add < MotionSharpness && MotionSharpness < 0.0f))
            add = MotionSharpness;
    
        setSharpness += add;

        if (setSharpness > 1.3f)
            setSharpness = 1.3f;
        else if (setSharpness < 0.0f)
            setSharpness = 0.0f;
    }
    
    float3 e = Source.Load(int3(DTid.x, DTid.y, 0)).rgb;
  
    // skip sharpening if set value == 0
    if (setSharpness == 0.0f)
    {
        if (Debug > 0 && DynamicSharpenEnabled > 0 && Sharpness > 0)
            e.g *= 1 + (12.0f * Sharpness);

        Dest[DTid.xy] = e;
        return;
    }

    float3 b = Source.Load(int3(DTid.x, DTid.y - 1, 0)).rgb;
    float3 d = Source.Load(int3(DTid.x - 1, DTid.y, 0)).rgb;
    float3 f = Source.Load(int3(DTid.x + 1, DTid.y, 0)).rgb;
    float3 h = Source.Load(int3(DTid.x, DTid.y + 1, 0)).rgb;
  
    // Min and max of ring.
    float3 minRGB = min(min(b, d), min(f, h));
    float3 maxRGB = max(max(b, d), max(f, h));
  
    // Immediate constants for peak range.
    float2 peakC = float2(1.0, -4.0);
  
    // Standard RCAS limiters
    float3 hitMin = minRGB * rcp(4.0 * maxRGB);
    float3 hitMax = (peakC.xxx - maxRGB) * rcp(4.0 * minRGB + peakC.yyy);
    float3 lobeRGB = max(-hitMin, hitMax);
    float lobe = max(-0.1875, min(max(lobeRGB.r, max(lobeRGB.g, lobeRGB.b)), 0.0)) * setSharpness;
    
    // Apply contrast adaptation only if Contrast > 0
    if (Contrast >= -10.0)
    {
        // Smooth minimum distance to signal limit divided by smooth max (directly from CAS.fx)
        float3 amp = saturate(min(minRGB, 2.0 - maxRGB) / max(maxRGB, 1e-5));
        
        // Shaping amount based on local contrast
        amp = rsqrt(amp);
        
        // Calculate the contrast adaptation factor
        float peak = -3.0 * Contrast + 8.0;
        float contrastFactor = 1.0 / max(amp.g * peak, 1.0); // Using green as representative
        
        // Apply contrast modulation - more subtle approach
        // This scales lobe strength based on local contrast without introducing softness
        lobe *= lerp(1.0, contrastFactor, Contrast); // Reduced intensity of effect with 0.5 multiplier
    }
    
    // Resolve with medium precision rcp
    float rcpL = rcp(4.0 * lobe + 1.0);
    float3 output = ((b + d + f + h) * lobe + e) * rcpL;
  
    if (Debug > 0 && DynamicSharpenEnabled > 0)
    {
        if (Sharpness < setSharpness)
            output.r *= 1 + (12.0f * (setSharpness - Sharpness));
        else
            output.g *= 1 + (12.0f * (Sharpness - setSharpness));
    }
  
    Dest[DTid.xy] = output;
}
)";

static ID3DBlob* RCAS_CompileShader(const char* shaderCode, const char* entryPoint, const char* target)
{
    ID3DBlob* shaderBlob = nullptr;
    ID3DBlob* errorBlob = nullptr;

    HRESULT hr = D3DCompile(shaderCode, strlen(shaderCode), nullptr, nullptr, nullptr, entryPoint, target, D3DCOMPILE_OPTIMIZATION_LEVEL3, 0, &shaderBlob, &errorBlob);

    if (FAILED(hr))
    {
        LOG_ERROR("error while compiling shader");

        if (errorBlob)
        {
            LOG_ERROR("error while compiling shader : {0}", (char*)errorBlob->GetBufferPointer());
            errorBlob->Release();
        }

        if (shaderBlob)
            shaderBlob->Release();

        return nullptr;
    }

    if (errorBlob)
        errorBlob->Release();

    return shaderBlob;
}