struct vs_out
{
  float3 world_normal : WORLD_NORMAL;
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

void main(in vs_out input, out float3 out_color : SV_Target0)
{
  float diffuse = max(dot(normalize(input.world_normal), sc.light_dir), 0.0);
  out_color = sc.ambient_color + diffuse * sc.light_color * ob.object_color;
}