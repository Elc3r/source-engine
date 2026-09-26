// Original texture probe; deliberately preserves all four sampled channels.
sampler2D sourceTexture : register(s0);
float4 main(float2 uv : TEXCOORD0) : COLOR0
{
    return tex2D(sourceTexture, uv);
}
