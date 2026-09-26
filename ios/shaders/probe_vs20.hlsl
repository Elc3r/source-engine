// Original texture probe, compiled by FXC into Shader Model 2 bytecode.
struct Input { float4 position : POSITION; float2 uv : TEXCOORD0; };
struct Output { float4 position : POSITION; float2 uv : TEXCOORD0; };
Output main(Input input)
{
    Output output;
    output.position = input.position;
    output.uv = input.uv;
    return output;
}
