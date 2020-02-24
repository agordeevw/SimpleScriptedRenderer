struct vs_in
{
  float3 position : POSITION;
  float3 normal : NORMAL;
};

struct vs_out
{
  float3 world_normal : WORLD_NORMAL;
  float4 screen_position : SV_Position;
};

struct scene_constants
{
  float4x4 world_to_screen;
  float3 light_dir;
  float _pad0;
  float3 light_color;
  float _pad1;
  float3 ambient_color;
  float _pad2;
};

struct object_constants
{
  float4x4 local_to_world;
  float4x4 world_to_local_transposed;
  float3 object_color;
  float _pad0;
};

cbuffer scene_constants : register(b0)
{
  scene_constants sc;
};

cbuffer object_constants : register(b1)
{
  object_constants ob;
};

void main(in vs_in input, out vs_out output)
{
  output.world_normal = mul(ob.world_to_local_transposed, float4(input.normal, 0.0)).xyz;
  output.world_normal = normalize(output.world_normal);
  output.screen_position = mul(sc.world_to_screen, mul(ob.local_to_world, float4(input.position, 1.0)));
}
