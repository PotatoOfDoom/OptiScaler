Texture2D<float4> SourceTexture : register(t0);
RWTexture2D<uint> DestinationTexture : register(u0); // B8R8G8A8_UNORM destination texture

// Shader to perform the conversion
[numthreads(512, 1, 1)]
void CSMain(uint3 dispatchThreadID : SV_DispatchThreadID)
{
    // Read from the source texture
    float4 srcColor = SourceTexture.Load(int3(dispatchThreadID.xy, 0));

    // Clamp color channels to [0, 1] range, as R8G8B8A8_UNORM is normalized
    srcColor = saturate(srcColor); // Ensures all values are between 0 and 1

    // Convert each channel to its corresponding bit range
    uint R = (uint) (srcColor.r * 255.0f); // 8 bits for Red
    uint G = (uint) (srcColor.g * 255.0f); // 8 bits for Green
    uint B = (uint) (srcColor.b * 255.0f); // 8 bits for Blue
    uint A = (uint) (srcColor.a * 255.0f); // 8 bits for Alpha

    // Pack the values into a single 32-bit unsigned int
    uint packedColor = B | R << 8 | G << 16 | A << 24;

    // Write the packed color to the destination texture
    DestinationTexture[dispatchThreadID.xy] = packedColor;
}