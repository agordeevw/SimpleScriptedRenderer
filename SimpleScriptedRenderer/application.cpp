#include <SDL.h>
#include <SDL_syswm.h>
#pragma warning(push)
#pragma warning(disable: 4201)
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#pragma warning(pop)

#include "imgui.h"
#include "imgui_impl_sdl.h"
#include "imgui_impl_dx11.h"

#include "application.hpp"
#include "object_pool.hpp"
#include "ring_buffer.hpp"
#include "static_string.hpp"
#include "static_vector.hpp"
#include "shader_bytecodes.h"
#include "vector.hpp"

// TODO: invent scripting support. Think about structure of engine API.
//  Store it in a table.

// In-engine console.
// Used to manipulate lua state and engine components via lua functions.
namespace console
{
static constexpr u32 MAX_ENTRY_SIZE = 512;
static constexpr u32 MAX_ENTRIES = 64;
static constexpr u32 MAX_INPUT_LOG_ENTRIES = 64;

static ring_buffer<static_string<MAX_ENTRY_SIZE>> g_log{ MAX_ENTRIES };
static ring_buffer<static_string<MAX_ENTRY_SIZE>> g_input_log{ MAX_INPUT_LOG_ENTRIES };
static bool g_active = false;
static u32 g_current_input_log_entry = 0;

static void interpret(lua_State* lua, const char* input_buffer)
{
  if (g_input_log.size() == g_input_log.capacity()) g_input_log.pop_front();
  g_input_log.push_back({ input_buffer });
  g_current_input_log_entry = g_input_log.size();

  {
    char console_buffer[2 * MAX_ENTRY_SIZE];
    sprintf(console_buffer, "> %s", input_buffer);
    if (g_log.size() == g_log.capacity()) g_log.pop_front();
    g_log.push_back({ console_buffer });
  }
  lua_getglobal(lua, "print");
  int top = lua_gettop(lua);
  // execute as expression
  char exec_buffer[1024];
  sprintf(exec_buffer, "return %s", input_buffer);
  if (luaL_dostring(lua, exec_buffer))
  {
    // failed, execute as statement
    lua_pop(lua, 1);
    if (luaL_dostring(lua, input_buffer))
    {
      // failed completely...
      char report_buffer[MAX_ENTRY_SIZE];
      sprintf(report_buffer, "error: %s", lua_tostring(lua, -1));
      if (g_log.size() == g_log.capacity()) g_log.pop_front();
      g_log.push_back({ report_buffer });
      lua_pop(lua, 1);
    }
  }
  else
  {
    // print statement returns
    int nargs = lua_gettop(lua) - top;
    lua_pcall(lua, nargs, 0, 0);
  }
}

static int imgui_input_callback(ImGuiInputTextCallbackData *data)
{
  if (g_input_log.size() == 0) return 0;
  if (data->EventKey == ImGuiKey_UpArrow)
  {
    if (g_current_input_log_entry == 0) return 0;
    g_current_input_log_entry--;
  }
  else if (data->EventKey == ImGuiKey_DownArrow)
  {
    // Behavior is poorly defined, that's why these checks are here...
    if (g_current_input_log_entry == g_input_log.size()) return 0;
    if (g_current_input_log_entry == g_input_log.size() - 1) return 0;
    g_current_input_log_entry++;
  }
  const i32 text_length = sprintf(data->Buf, "%s", g_input_log[g_current_input_log_entry].c_str());
  data->BufDirty = true;
  data->BufTextLen = text_length;
  data->CursorPos = text_length;
  data->SelectionStart = 0;
  data->SelectionEnd = 0;
  return 0;
}
} // namespace console

// All mesh-related data.
// Vertex-index buffers for IA stage.
// AABB for frustum culling.
struct vertex_data
{
  com_ptr<ID3D11Buffer> data;
  u32 index_data_offset;
  u32 index_count;
  glm::vec3 aabb_center;
  glm::vec3 aabb_extent;
};

// Vertex description.
// Should include:
//  Position, normal, tangent (binormal inferred). Common for many meshes.
//  UV channels (texture coordinates, morph displacements). Optional.
//  Blend weights + blend indices (for skeletal meshes). Required for skeletal meshes.
// Preferably in separate buffers/separate parts of buffers.
struct vertex
{
  glm::vec3 position;
  glm::vec3 normal;
};

// TODO: Skeleton + skeleton pose.

// BEGIN: Mesh data

static vertex_data create_vertex_data(d3d11_renderer& renderer, vector<vertex> const& vertices, vector<u32> const& indices)
{
  vertex_data ret;
  const u32 vertex_array_size = vertices.size() * sizeof(vertices[0]);
  const u32 index_array_size = indices.size() * sizeof(indices[0]);

  char* buffer_data = new char[vertex_array_size + index_array_size];
  memcpy(buffer_data, vertices.data(), vertex_array_size);
  memcpy(buffer_data + vertex_array_size, indices.data(), index_array_size);
  ret.index_data_offset = vertex_array_size;
  ret.index_count = indices.size();

  glm::vec3 aabb_min = vertices[0].position;
  glm::vec3 aabb_max = vertices[0].position;
  for (u32 i = 0; i < vertices.size(); i++)
  {
    aabb_min = glm::min(aabb_min, vertices[i].position);
    aabb_max = glm::max(aabb_max, vertices[i].position);
  }
  ret.aabb_center = 0.5f * (aabb_max + aabb_min);
  ret.aabb_extent = 0.5f * (aabb_max - aabb_min);

  {
    HRESULT hr;

    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = vertex_array_size + index_array_size;
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_INDEX_BUFFER;

    D3D11_SUBRESOURCE_DATA initial_data = {};
    initial_data.pSysMem = buffer_data;
    hr = renderer.device->CreateBuffer(&desc, &initial_data, ret.data.ReleaseAndGetAddressOf());
    my_assert(SUCCEEDED(hr));
  }

  return ret;
}

static vector<vertex_data> g_vds = {};

static void create_vds(d3d11_renderer& renderer)
{
  // Triangle
  {
    vector<vertex> verts;
    vector<u32> indices;

    verts.push_back({ {-sqrt(3.0f) * 0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, +1.0f} });
    verts.push_back({ {+0.0f, +sqrt(3.0f) * 0.5f, 0.0f}, {0.0f, 0.0f, +1.0f} });
    verts.push_back({ {+sqrt(3.0f) * 0.5f, -0.5f, 0.0f}, {0.0f, 0.0f, +1.0f} });
    indices.push_back(0);
    indices.push_back(1);
    indices.push_back(2);

    g_vds.push_back(create_vertex_data(renderer, verts, indices));
  }
  // Cube
  {
    vector<vertex> verts;
    vector<u32> indices;

    // left side
    verts.push_back({ {-0.5f, +0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f} });
    verts.push_back({ {-0.5f, +0.5f, +0.5f}, {-1.0f, 0.0f, 0.0f} });
    verts.push_back({ {-0.5f, -0.5f, +0.5f}, {-1.0f, 0.0f, 0.0f} });
    verts.push_back({ {-0.5f, -0.5f, -0.5f}, {-1.0f, 0.0f, 0.0f} });
    indices.push_back(0);
    indices.push_back(1);
    indices.push_back(2);
    indices.push_back(0);
    indices.push_back(2);
    indices.push_back(3);
    // right side
    verts.push_back({ {+0.5f, +0.5f, +0.5f}, {+1.0f, 0.0f, 0.0f} });
    verts.push_back({ {+0.5f, +0.5f, -0.5f}, {+1.0f, 0.0f, 0.0f} });
    verts.push_back({ {+0.5f, -0.5f, -0.5f}, {+1.0f, 0.0f, 0.0f} });
    verts.push_back({ {+0.5f, -0.5f, +0.5f}, {+1.0f, 0.0f, 0.0f} });
    indices.push_back(4);
    indices.push_back(5);
    indices.push_back(6);
    indices.push_back(4);
    indices.push_back(6);
    indices.push_back(7);
    // top side
    verts.push_back({ {-0.5f, +0.5f, -0.5f}, {0.0f, +1.0f, 0.0f} });
    verts.push_back({ {+0.5f, +0.5f, -0.5f}, {0.0f, +1.0f, 0.0f} });
    verts.push_back({ {+0.5f, +0.5f, +0.5f}, {0.0f, +1.0f, 0.0f} });
    verts.push_back({ {-0.5f, +0.5f, +0.5f}, {0.0f, +1.0f, 0.0f} });
    indices.push_back(8);
    indices.push_back(9);
    indices.push_back(10);
    indices.push_back(8);
    indices.push_back(10);
    indices.push_back(11);
    // bottom side
    verts.push_back({ {-0.5f, -0.5f, +0.5f}, {0.0f, -1.0f, 0.0f} });
    verts.push_back({ {+0.5f, -0.5f, +0.5f}, {0.0f, -1.0f, 0.0f} });
    verts.push_back({ {+0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f} });
    verts.push_back({ {-0.5f, -0.5f, -0.5f}, {0.0f, -1.0f, 0.0f} });
    indices.push_back(12);
    indices.push_back(13);
    indices.push_back(14);
    indices.push_back(12);
    indices.push_back(14);
    indices.push_back(15);
    // face side
    verts.push_back({ {-0.5f, +0.5f, +0.5f}, {0.0f, 0.0f, +1.0f} });
    verts.push_back({ {+0.5f, +0.5f, +0.5f}, {0.0f, 0.0f, +1.0f} });
    verts.push_back({ {+0.5f, -0.5f, +0.5f}, {0.0f, 0.0f, +1.0f} });
    verts.push_back({ {-0.5f, -0.5f, +0.5f}, {0.0f, 0.0f, +1.0f} });
    indices.push_back(16);
    indices.push_back(17);
    indices.push_back(18);
    indices.push_back(16);
    indices.push_back(18);
    indices.push_back(19);
    // back side
    verts.push_back({ {+0.5f, +0.5f, -0.5f}, {0.0f, 0.0f, -1.0f} });
    verts.push_back({ {-0.5f, +0.5f, -0.5f}, {0.0f, 0.0f, -1.0f} });
    verts.push_back({ {-0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f} });
    verts.push_back({ {+0.5f, -0.5f, -0.5f}, {0.0f, 0.0f, -1.0f} });
    indices.push_back(20);
    indices.push_back(21);
    indices.push_back(22);
    indices.push_back(20);
    indices.push_back(22);
    indices.push_back(23);

    g_vds.push_back(create_vertex_data(renderer, verts, indices));
  }
}

static void destroy_vds()
{
  g_vds = vector<vertex_data>{};
}

// END: Mesh data

// Scene constants buffer.
// Probably should be separated for vertex/pixel shaders.
// Filled once each frame.
struct scene_constants
{
  glm::mat4x4 world_to_screen;
  glm::vec3 light_dir;
  f32 _pad0;
  glm::vec3 light_color;
  f32 _pad1;
  glm::vec3 ambient_color;
  f32 _pad2;
};

// Object constants buffer.
// Probably should be separated for vertex/pixel shaders.
// Should separate object data and material data.
struct object_constants
{
  glm::mat4x4 local_to_world;
  glm::mat4x4 world_to_local_transposed;
  glm::vec3 object_color;
  f32 _pad0;
};

// TODO: optimize to minimize pipeline state changes.
//  Mesh = list of submeshes + all that hubbub common for vertex data.
//  Sort drawcalls by material.

static scene_constants g_scene_constants;
static object_constants g_object_constants;

// Per material. Common part in shaders.
static com_ptr<ID3D11VertexShader> g_vs;
// Per material. Common part in shaders.
static com_ptr<ID3D11PixelShader> g_ps;
// U-uh... Per material? Meshes should conform...
static com_ptr<ID3D11InputLayout> g_input_layout;
// Read up on proper usage of constant buffers.
// Because having one buffer per material smells less than great...
static com_ptr<ID3D11Buffer> g_buf_scene_constants;
static com_ptr<ID3D11Buffer> g_buf_object_constants;
// How to manage these states by material?
// Also, have default materials and states for only-in-engine parts.
static com_ptr<ID3D11BlendState> g_blend_state;
static com_ptr<ID3D11RasterizerState> g_rasterizer_state_solid;
static com_ptr<ID3D11RasterizerState> g_rasterizer_state_wireframe;
static com_ptr<ID3D11DepthStencilState> g_depth_stencil_state;

static void create_common_pipeline_objects(d3d11_renderer& renderer)
{
  HRESULT hr;

  {
    hr = renderer.device->CreateVertexShader(vs_bytecode, sizeof(vs_bytecode), nullptr, g_vs.ReleaseAndGetAddressOf());
    my_assert(SUCCEEDED(hr));
    hr = renderer.device->CreatePixelShader(ps_bytecode, sizeof(ps_bytecode), nullptr, g_ps.ReleaseAndGetAddressOf());
    my_assert(SUCCEEDED(hr));
  }

  {
    D3D11_INPUT_ELEMENT_DESC elem_descs[2] = {};
    elem_descs[0].SemanticName = "POSITION";
    elem_descs[0].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    elem_descs[0].AlignedByteOffset = 0;
    elem_descs[1].SemanticName = "NORMAL";
    elem_descs[1].Format = DXGI_FORMAT_R32G32B32_FLOAT;
    elem_descs[1].AlignedByteOffset = 12;
    hr = renderer.device->CreateInputLayout(elem_descs, 2, vs_bytecode, sizeof(vs_bytecode), g_input_layout.ReleaseAndGetAddressOf());
    my_assert(SUCCEEDED(hr));
  }

  {
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = sizeof(g_object_constants);
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = renderer.device->CreateBuffer(&desc, nullptr, g_buf_object_constants.ReleaseAndGetAddressOf());
    my_assert(SUCCEEDED(hr));
  }

  {
    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = sizeof(g_scene_constants);
    desc.Usage = D3D11_USAGE_DYNAMIC;
    desc.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
    desc.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
    hr = renderer.device->CreateBuffer(&desc, nullptr, g_buf_scene_constants.ReleaseAndGetAddressOf());
    my_assert(SUCCEEDED(hr));
  }

  {
    D3D11_BLEND_DESC desc = {};
    desc.RenderTarget[0].BlendEnable = FALSE;
    desc.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
    hr = renderer.device->CreateBlendState(&desc, g_blend_state.ReleaseAndGetAddressOf());
    my_assert(SUCCEEDED(hr));
  }

  {
    D3D11_RASTERIZER_DESC desc = {};
    desc.FillMode = D3D11_FILL_SOLID;
    desc.CullMode = D3D11_CULL_BACK;
    hr = renderer.device->CreateRasterizerState(&desc, g_rasterizer_state_solid.ReleaseAndGetAddressOf());
    my_assert(SUCCEEDED(hr));
  }

  {
    D3D11_RASTERIZER_DESC desc = {};
    desc.FillMode = D3D11_FILL_WIREFRAME;
    desc.CullMode = D3D11_CULL_NONE;
    hr = renderer.device->CreateRasterizerState(&desc, g_rasterizer_state_wireframe.ReleaseAndGetAddressOf());
    my_assert(SUCCEEDED(hr));
  }

  {
    D3D11_DEPTH_STENCIL_DESC desc = {};
    desc.DepthEnable = TRUE;
    desc.DepthFunc = D3D11_COMPARISON_LESS;
    desc.DepthWriteMask = D3D11_DEPTH_WRITE_MASK_ALL;
    hr = renderer.device->CreateDepthStencilState(&desc, g_depth_stencil_state.ReleaseAndGetAddressOf());
    my_assert(SUCCEEDED(hr));
  }
}

static void destroy_common_pipeline_objects()
{
  g_depth_stencil_state.Reset();
  g_rasterizer_state_wireframe.Reset();
  g_rasterizer_state_solid.Reset();
  g_blend_state.Reset();
  g_buf_object_constants.Reset();
  g_buf_scene_constants.Reset();
  g_input_layout.Reset();
  g_ps.Reset();
  g_vs.Reset();
}

// This is transform component.

struct transform
{
  glm::vec3 t = { 0.0f, 0.0f, 0.0f };
  glm::quat r = { 0.0f, 0.0f, 0.0f, 1.0f };
  glm::vec3 s = { 1.0f, 1.0f, 1.0f };

  glm::mat4x4 local_to_world() const
  {
    glm::mat4x4 m;
    m[0] = s.x * 2.0f * glm::vec4{ r.x * r.x + r.w * r.w - 0.5f, r.x * r.y + r.z * r.w, r.x * r.z - r.y * r.w, 0.0f };
    m[1] = s.y * 2.0f * glm::vec4{ r.y * r.x - r.z * r.w, r.y * r.y + r.w * r.w - 0.5f, r.y * r.z + r.x * r.w, 0.0f };
    m[2] = s.z * 2.0f * glm::vec4{ r.z * r.x + r.y * r.w, r.z * r.y - r.x * r.w, r.z * r.z + r.w * r.w - 0.5f, 0.0f };
    m[3] = { t.x, t.y, t.z, 1.0f };
    return m;
  }

  glm::mat4x4 world_to_local_transposed() const
  {
    // omit translation because this matrix is used to transform vectors
    glm::mat4x4 m;
    m[0] = (2.0f / s.x) * glm::vec4{ r.x * r.x + r.w * r.w - 0.5f, r.x * r.y + r.z * r.w, r.x * r.z - r.y * r.w, 0.0f };
    m[1] = (2.0f / s.y) * glm::vec4{ r.y * r.x - r.z * r.w, r.y * r.y + r.w * r.w - 0.5f, r.y * r.z + r.x * r.w, 0.0f };
    m[2] = (2.0f / s.z) * glm::vec4{ r.z * r.x + r.y * r.w, r.z * r.y - r.x * r.w, r.z * r.z + r.w * r.w - 0.5f, 0.0f };
    m[3] = { 0.0f, 0.0f, 0.0f, 1.0f };
    return m;
  }
};

// This is camera component.

struct camera
{
  transform tr = {};
  float fov_degrees = 45.0f;
  float z_near = 0.1f;
  float z_far = 80.0f;
  float aspect = 1.0f;

  glm::mat4x4 world_to_view() const
  {
    const glm::vec3& t = tr.t;
    const glm::quat& r = tr.r;
    glm::mat4x4 m;
    m[0] = 2.0f * glm::vec4{ r.x * r.x + r.w * r.w - 0.5f, r.x * r.y - r.z * r.w, r.x * r.z + r.y * r.w, 0.0f };
    m[1] = 2.0f * glm::vec4{ r.y * r.x + r.z * r.w, r.y * r.y + r.w * r.w - 0.5f, r.y * r.z - r.x * r.w, 0.0f };
    m[2] = 2.0f * glm::vec4{ r.z * r.x - r.y * r.w, r.z * r.y + r.x * r.w, r.z * r.z + r.w * r.w - 0.5f, 0.0f };
    m[3] = {
      -(m[0][0] * t.x + m[1][0] * t.y + m[2][0] * t.z),
      -(m[0][1] * t.x + m[1][1] * t.y + m[2][1] * t.z),
      -(m[0][2] * t.x + m[1][2] * t.y + m[2][2] * t.z),
      1.0f };
    return m;
  }

  glm::mat4x4 view_to_screen() const
  {
    const f32 h = 1.0f / tan(glm::radians(fov_degrees) * 0.5f);
    const f32 f = z_far / (z_near - z_far);
    glm::mat4x4 p = {};
    p[0][0] = h / aspect;
    p[1][1] = h;
    p[2][2] = f;
    p[2][3] = -1.0f;
    p[3][2] = z_near * f;
    return p;
  }

  glm::mat4x4 world_to_screen() const
  {
    return view_to_screen() * world_to_view();
  }
};

// Entity. Hello there. Root object has a pool of these.

struct entity
{
  static_string<32> name = {};
  transform tr = {};
  const vertex_data* vd = nullptr;
  glm::vec3 color = { 0.5f, 0.8f, 0.5f };
  entity* parent = nullptr;
};

// Render system uses this for frustum culling.

static bool aabb_view_frustum_intersection(const camera& cam, const vertex_data& vd, const transform& tr)
{
  // Points of AABB
  // c - center, e - extent
  // p = c + s * e, where s = {-1, -1, -1} ... {1, 1, 1}
  // T - local to view transform matrix
  // T(c+s*e) = T(c) + T(s*e)
  // T(s*e).x = T.xx * s.x * e.x + T.xy * s.y * e.y + T.xz * s.z * e.z
  // T(s*e).y = T.yx * s.x * e.x + T.yy * s.y * e.y + T.yz * s.z * e.z
  // T(s*e).z = T.zx * s.x * e.x + T.zy * s.y * e.y + T.zz * s.z * e.z
  // let cx = e.x * (T.xx, T.yx, T.zx),
  // let cy = e.y * (T.xy, T.yy, T.zy),
  // let cz = e.z * (T.xz, T.yz, T.zz),
  // then T(s*e) = cx * s.x + cy * s.y + cz * s.z
  const glm::mat4x4 tr_mat = cam.world_to_view() * tr.local_to_world();
  const glm::vec4 c = tr_mat * glm::vec4{ vd.aabb_center, 1.0f };
  const glm::vec4 cx = vd.aabb_extent[0] * tr_mat[0];
  const glm::vec4 cy = vd.aabb_extent[1] * tr_mat[1];
  const glm::vec4 cz = vd.aabb_extent[2] * tr_mat[2];
  const glm::vec4 corners[8]
  {
    c + cx + cy + cz,
    c + cx + cy - cz,
    c + cx - cy + cz,
    c + cx - cy - cz,
    c - cx + cy + cz,
    c - cx + cy - cz,
    c - cx - cy + cz,
    c - cx - cy - cz,
  };

  // Frustum plane normals are pointed outside the view frustum.
  const f32 t = tan(glm::radians(cam.fov_degrees) * 0.5f);
  const f32 a = cam.aspect;
  const glm::vec4 frustum[6]
  {
    {0.0f, 0.0f, +1.0f, -cam.z_near},
    {0.0f, 0.0f, -1.0f, -cam.z_far},
    {+1.0f, 0.0f, a * t, 0.0f},
    {-1.0f, 0.0f, a * t, 0.0f},
    {0.0f, +1.0f, t, 0.0f},
    {0.0f, -1.0f, t, 0.0f},
  };
  for (i32 plane = 0; plane < 6; plane++)
  {
    int i = 0;
    int o = 0;
    for (i32 vert = 0; vert < 8 && (i == 0 || o == 0); vert++)
    {
      const glm::vec4& v = corners[vert];
      const glm::vec4& p = frustum[plane];
      int d = v.x * p.x + v.y * p.y + v.z * p.z > -p.w;
      o |= d;
      i |= !d;
    }
    if (!i) return false; // no points inside the frustum
    if (o) return true;   // points are on both sides of the frustum plane
  }
  return true; // all points inside the frustum
}

// This is a root object.

struct scene
{
  glm::vec3 light_dir = glm::normalize(glm::vec3{ 1.0f, 1.0f, 1.0f });
  glm::vec3 light_color = glm::vec3{ 1.0f, 1.0f, 1.0f };
  glm::vec3 ambient_color = glm::vec3{ 0.05f, 0.05f, 0.05f };
  camera cam = {};
  vector<entity*> entities;
  object_pool<entity> entity_pool = { 1024 * 1024 };
};

// Render system uses this to render everything.

static u32 g_num_visible = 0;

static void render_scene(d3d11_renderer& renderer, const scene& sc,
                         glm::vec2 viewport_pos, glm::vec2 viewport_size)
{
  g_scene_constants.ambient_color = sc.ambient_color;
  g_scene_constants.light_color = sc.light_color;
  g_scene_constants.light_dir = glm::normalize(sc.light_dir);
  g_scene_constants.world_to_screen = sc.cam.world_to_screen();
  {
    D3D11_MAPPED_SUBRESOURCE mapped;
    renderer.ctx->Map(g_buf_scene_constants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
    memcpy(mapped.pData, &g_scene_constants, sizeof(g_scene_constants));
    renderer.ctx->Unmap(g_buf_scene_constants.Get(), 0);
  }

  renderer.ctx->OMSetBlendState(g_blend_state.Get(), nullptr, 0xFFFFFFFu);
  renderer.ctx->OMSetDepthStencilState(g_depth_stencil_state.Get(), 0);
  renderer.ctx->OMSetRenderTargets(1, renderer.swapchain_rtv.GetAddressOf(), renderer.dsv.Get());

  g_num_visible = 0;
  for (u32 i = 0; i < sc.entities.size(); i++)
  {
    entity* e = sc.entities[i];
    if (e->vd == nullptr || aabb_view_frustum_intersection(sc.cam, *e->vd, e->tr) == false)
      continue;
    g_num_visible++;
    glm::mat4x4 ltw = e->tr.local_to_world();
    glm::mat4x4 wtlt = e->tr.world_to_local_transposed();
    {
      entity* pe = e->parent;
      while (pe)
      {
        ltw = pe->tr.local_to_world() * ltw;
        wtlt = pe->tr.world_to_local_transposed() * wtlt;
        pe = pe->parent;
      }
    }
    g_object_constants.local_to_world = ltw;
    g_object_constants.world_to_local_transposed = wtlt;
    g_object_constants.object_color = e->color;
    {
      D3D11_MAPPED_SUBRESOURCE mapped;
      renderer.ctx->Map(g_buf_object_constants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
      memcpy(mapped.pData, &g_object_constants, sizeof(g_object_constants));
      renderer.ctx->Unmap(g_buf_object_constants.Get(), 0);
    }
    u32 stride = sizeof(vertex);
    u32 offset = 0;
    renderer.ctx->IASetVertexBuffers(0, 1, e->vd->data.GetAddressOf(), &stride, &offset);
    renderer.ctx->IASetIndexBuffer(e->vd->data.Get(), DXGI_FORMAT_R32_UINT, e->vd->index_data_offset);
    renderer.ctx->IASetInputLayout(g_input_layout.Get());
    renderer.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    renderer.ctx->VSSetShader(g_vs.Get(), nullptr, 0);
    renderer.ctx->VSSetConstantBuffers(0, 1, g_buf_scene_constants.GetAddressOf());
    renderer.ctx->VSSetConstantBuffers(1, 1, g_buf_object_constants.GetAddressOf());
    renderer.ctx->PSSetShader(g_ps.Get(), nullptr, 0);
    renderer.ctx->PSSetConstantBuffers(0, 1, g_buf_scene_constants.GetAddressOf());
    renderer.ctx->PSSetConstantBuffers(1, 1, g_buf_object_constants.GetAddressOf());
    renderer.ctx->RSSetState(g_rasterizer_state_solid.Get());
    D3D11_VIEWPORT vp = {};
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    vp.TopLeftX = viewport_pos.x;
    vp.TopLeftY = viewport_pos.y;
    vp.Width = viewport_size.x;
    vp.Height = viewport_size.y;
    renderer.ctx->RSSetViewports(1, &vp);
    renderer.ctx->DrawIndexed(e->vd->index_count, 0, 0);
  }
}

static scene g_scene = {};

// Exported function to manipulate scene. Make more of these to extend console capabilities.

static int luaexport_set_light_dir(lua_State* lua)
{
  glm::vec3 dir;
  dir[0] = (f32)luaL_checknumber(lua, 1);
  dir[1] = (f32)luaL_checknumber(lua, 2);
  dir[2] = (f32)luaL_checknumber(lua, 3);
  if (dir[0] == 0.0f && dir[1] == 0.0f && dir[2] == 0.0f)
  {
    lua_pushstring(lua, "nonzero vector expected");
    return lua_error(lua);
  }
  g_scene.light_dir = glm::normalize(dir);
  return 0;
}

// Function to set up default scene.
// Redo in terms of components and entities.

static void setup_scene(scene& sc)
{
  sc.light_dir = glm::normalize(glm::vec3{ 1.0f, 0.5f, 0.75f });
  sc.cam.tr.t = { 0.0f, 0.0f, 24.0f };
  const f32 r = 8.0f;
  const f32 s = 1.0f;
  for (f32 x = -r; x <= r; x += s)
  {
    for (f32 y = -r; y <= r; y += s)
    {
      for (f32 z = -r; z <= r; z += s)
      {
        if (sc.entity_pool.size() == sc.entity_pool.capacity())
        {
          return;
        }
        sc.entities.push_back(sc.entity_pool.construct());
        entity* e = sc.entities.back();
        e->vd = &g_vds[1];
        e->tr.t.x = x;
        e->tr.t.y = y;
        e->tr.t.z = z;
        e->tr.s.x = 0.5f;
        e->tr.s.y = 0.5f;
        e->tr.s.z = 0.5f;
        e->color.x = (x + r) / (r * 2.0f);
        e->color.y = (y + r) / (r * 2.0f);
        e->color.z = (z + r) / (r * 2.0f);
      }
    }
  }
}

// Exported function to print to console.
static int luaexport_print(lua_State* lua)
{
  char text_buffer[console::MAX_ENTRY_SIZE];
  text_buffer[0] = 0;
  char* text = text_buffer;
  const int nargs = lua_gettop(lua);
  for (int i = 1; i <= nargs; i++)
  {
    const char *s = luaL_tolstring(lua, i, nullptr);
    if (s == nullptr)
    {
      lua_pushstring(lua, "print: conversion of argument to string failed");
      lua_error(lua);
    }
    if (i > 1) text += sprintf(text, "\t");
    text += sprintf(text, "%s", s);
    lua_pop(lua, 1);
  }

  if (console::g_log.size() == console::g_log.capacity()) console::g_log.pop_front();
  console::g_log.push_back({ text_buffer });
  return 0;
}

// Setup lua state.
// Load necessary libraries (nothing more).
// Scripting state. Table per entity? Not sure...
// Engine table for engine API to be used in scripting components.
// Scripting component = object + functions + reference to owner entity.

static void setup_lua(lua_State** pLua)
{
  *pLua = luaL_newstate();
  lua_State* lua = *pLua;
  luaL_openlibs(lua);
  lua_pushglobaltable(lua);
  lua_pushcfunction(lua, luaexport_print);
  lua_setfield(lua, -2, "print");
  lua_pushcfunction(lua, luaexport_set_light_dir);
  lua_setfield(lua, -2, "set_light_dir");
  lua_pop(lua, 1);
}

application::application(SDL_Window* window) :
  m_window{ window }
{
  SDL_SysWMinfo info;
  SDL_VERSION(&info.version);
  SDL_GetWindowWMInfo(window, &info);
  HWND hwnd = info.info.win.window;
  renderer.init(hwnd);

  IMGUI_CHECKVERSION();
  ImGui::CreateContext();
  ImGui::StyleColorsDark();
  ImGui_ImplSDL2_InitForD3D(window);
  ImGui_ImplDX11_Init(renderer.device.Get(), renderer.ctx.Get());

  create_vds(renderer);
  create_common_pipeline_objects(renderer);

  setup_lua(&lua);

  setup_scene(g_scene);
}

application::~application()
{
  lua_close(lua);

  destroy_common_pipeline_objects();
  destroy_vds();

  ImGui_ImplDX11_Shutdown();
  ImGui_ImplSDL2_Shutdown();
  ImGui::DestroyContext();

  renderer.shutdown();
}

// Try and make UI. How to handle mouse clicks?? Peek at ImGUI...

// Input part.
// Rewrite it using SDL events per frame.
// Add timestamps for key events to track chords or something...

static i32 g_mouse_dx = 0;
static i32 g_mouse_dy = 0;
static i32 g_vsync = 0;

void application::main_loop()
{
  f64 previous_time = (f64)SDL_GetPerformanceCounter() / (f64)SDL_GetPerformanceFrequency();
  f64 lag = 0.0;
  bool loop_active = true;
  while (loop_active)
  {
    f64 current_time = (f64)SDL_GetPerformanceCounter() / (f64)SDL_GetPerformanceFrequency();
    f64 elapsed_time = current_time - previous_time;
    previous_time = current_time;
    lag += elapsed_time;

    const f64 delta_time = elapsed_time;
    m_input.update();
    g_mouse_dx = 0;
    g_mouse_dy = 0;

    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
      ImGui_ImplSDL2_ProcessEvent(&event);
      switch (event.type)
      {
        case SDL_QUIT:
        {
          return;
        }
        case SDL_WINDOWEVENT:
        {
          switch (event.window.event)
          {
            case SDL_WINDOWEVENT_RESIZED:
            {
              resize();
              break;
            }
          }
          break;
        }
        case SDL_MOUSEMOTION:
        {
          g_mouse_dx = event.motion.xrel;
          g_mouse_dy = event.motion.yrel;
          break;
        }
        case SDL_KEYDOWN: // fallthrough
        case SDL_KEYUP:
        break;
      }
    }

    // Fixed update is used for systems that require fixed timesteps.
    int updates_left = 2;
    while (lag >= seconds_per_update)
    {
      fixed_update(seconds_per_update);
      lag -= seconds_per_update;
      updates_left--;
      if (updates_left == 0) break;
    }

    // Update is used for systems that must be updated every frame.
    // How to make systems update once every several frames?
    update(delta_time);

    render();

    constexpr int vsync_disabled = 0;
    constexpr int vsync_d3d11 = 1;
    constexpr int vsync_my = 2;
    switch (g_vsync)
    {
      case vsync_d3d11:
      {
        renderer.swapchain->Present(1, 0);
        break;
      }
      case vsync_my:
      {
        if (elapsed_time < seconds_per_frame)
        {
          u32 delay_ms = (u32)((seconds_per_frame - elapsed_time) * 1000.0);
          SDL_Delay(delay_ms);
        }
        renderer.swapchain->Present(0, 0);
      }
      case vsync_disabled:
      default:
      {
        renderer.swapchain->Present(0, 0);
        break;
      }
    }
  }
}

void application::resize()
{
  i32 width, height;
  SDL_GetWindowSize(m_window, &width, &height);
  renderer.resize_swapchain(width, height);
}

// Camera controls. May actually be a part of a script.
// This one is for in-editor camera.

static f32 g_mouse_angle_x = 0.0f;
static f32 g_mouse_angle_y = 0.0f;
static bool g_camera_controls_active = false;
static i32 g_selected_entity = (i32)-1;

// Draw ImGUI here.
void application::update(f64 delta_time)
{
  // In-editor camera.
  if (g_camera_controls_active)
  {
    glm::vec3 cam_forward = g_scene.cam.tr.r * glm::vec3{ 0.0f, 0.0f, -1.0f };
    glm::vec3 cam_right = g_scene.cam.tr.r * glm::vec3{ 1.0f, 0.0f, 0.0f };
    const u8* keys = SDL_GetKeyboardState(nullptr);
    if (keys[SDL_SCANCODE_W])
      g_scene.cam.tr.t += 4.0f * cam_forward * (f32)delta_time;
    if (keys[SDL_SCANCODE_S])
      g_scene.cam.tr.t -= 4.0f * cam_forward * (f32)delta_time;
    if (keys[SDL_SCANCODE_D])
      g_scene.cam.tr.t += 4.0f * cam_right * (f32)delta_time;
    if (keys[SDL_SCANCODE_A])
      g_scene.cam.tr.t -= 4.0f * cam_right * (f32)delta_time;

    g_mouse_angle_x += g_mouse_dx * 0.00125f;
    g_mouse_angle_y += g_mouse_dy * 0.00125f;
    g_mouse_angle_y = glm::clamp(g_mouse_angle_y, -glm::pi<f32>() * 0.25f, glm::pi<f32>() * 0.25f);
  }
  g_scene.cam.tr.r = glm::angleAxis(g_mouse_angle_x, glm::vec3{ 0.0f, -1.0f, 0.0f })
    * glm::angleAxis(g_mouse_angle_y, glm::vec3{ -1.0f, 0.0f, 0.0f });

  // In-editor key bindings.
  if (m_input.key_pressed(SDL_SCANCODE_GRAVE))
  {
    console::g_active = !console::g_active;
  }

  if (m_input.key_pressed(SDL_SCANCODE_C))
  {
    g_camera_controls_active = !g_camera_controls_active;
    SDL_SetRelativeMouseMode(g_camera_controls_active ? SDL_TRUE : SDL_FALSE);
  }

  // In-editor UI.
  {
    ImGui_ImplDX11_NewFrame();
    ImGui_ImplSDL2_NewFrame(m_window);
    ImGui::NewFrame();

    ImGui::SetNextWindowPos({ 0, 0 });
    ImGui::Begin("Debug", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
    ImGui::Text("Frame time: %5.2lf ms", delta_time * 1000.0);
    ImGui::Text("Vsync");
    if (ImGui::RadioButton("disabled", g_vsync == 0)) g_vsync = 0;
    ImGui::SameLine();
    if (ImGui::RadioButton("d3d", g_vsync == 1)) g_vsync = 1;
    ImGui::SameLine();
    if (ImGui::RadioButton("my", g_vsync == 2)) g_vsync = 2;

    u32 sample_count = renderer.swapchain_desc.SampleDesc.Count;
    ImGui::Text("Sample count");
    if (ImGui::RadioButton("x1", sample_count == 1)) renderer.set_multisample_count(1);
    ImGui::SameLine();
    if (ImGui::RadioButton("MS x2", sample_count == 2)) renderer.set_multisample_count(2);
    ImGui::SameLine();
    if (ImGui::RadioButton("MS x4", sample_count == 4)) renderer.set_multisample_count(4);

    ImGui::Text("View frustum culling");
    ImGui::Text("Visible entities: %u", g_num_visible);

    ImGui::End();

    const f32 entities_window_width = 0.2f * (f32)renderer.swapchain_desc.BufferDesc.Width;
    ImGui::SetNextWindowPos({ (f32)renderer.swapchain_desc.BufferDesc.Width - entities_window_width, 0 });
    ImGui::SetNextWindowSize({ entities_window_width, (f32)renderer.swapchain_desc.BufferDesc.Height });
    ImGui::Begin("Entity", nullptr,
                 ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);

    ImGui::InputInt("Entity ID", &g_selected_entity);
    if (g_selected_entity >= 0 && (u32)g_selected_entity < g_scene.entities.size())
    {
      entity& e = *g_scene.entities[g_selected_entity];
      char name_buffer[console::MAX_ENTRY_SIZE];
      sprintf(name_buffer, "(%i)", g_selected_entity);
      ImGui::InputFloat("tX", &e.tr.t.x);
      ImGui::InputFloat("tY", &e.tr.t.y);
      ImGui::InputFloat("tZ", &e.tr.t.z);
      glm::vec3 euler = glm::degrees(glm::eulerAngles(e.tr.r));
      if (ImGui::InputFloat("rX", &euler.x, 0.0f, 0.0f, "%8.3f")
          | ImGui::InputFloat("rY", &euler.y, 0.0f, 0.0f, "%8.3f")
          | ImGui::InputFloat("rZ", &euler.z, 0.0f, 0.0f, "%8.3f"))
      {
        e.tr.r = glm::quat{ glm::radians(euler) };
      }
      ImGui::InputFloat("sX", &e.tr.s.x);
      ImGui::InputFloat("sY", &e.tr.s.y);
      ImGui::InputFloat("sZ", &e.tr.s.z);
    }
    ImGui::End();

    if (console::g_active)
    {
      static bool g_input_happened = false;

      ImGui::SetNextWindowPos({ 0, (f32)renderer.swapchain_desc.BufferDesc.Height - 300 });
      ImGui::SetNextWindowSize({ 600, 300 });
      if (ImGui::Begin("Console", nullptr,
                       ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse))
      {
        ImGui::SetKeyboardFocusHere();
        if (ImGui::BeginChild("Console log", { 0, -30 }, true), true)
        {
          for (u32 i = 0; i < console::g_log.size(); i++)
            ImGui::TextWrapped("%s", console::g_log[i].c_str());
          if (g_input_happened)
          {
            g_input_happened = false;
            ImGui::SetScrollHereY();
          }
        }
        ImGui::EndChild();
        char input_buffer[console::MAX_ENTRY_SIZE];
        input_buffer[0] = 0;
        if (ImGui::InputText("", input_buffer, sizeof(input_buffer),
                             ImGuiInputTextFlags_EnterReturnsTrue | ImGuiInputTextFlags_CallbackHistory,
                             console::imgui_input_callback, this))
        {
          g_input_happened = true;
          console::interpret(lua, input_buffer);
        }
      }
      ImGui::End();
    }

    ImGui::Render();
  }
}

void application::fixed_update(f64 delta_time)
{
  (void)delta_time;
}

void application::render()
{
  const f32 color[] = { 0.0f, 0.0f, 0.0f, 1.0f };
  renderer.ctx->ClearRenderTargetView(renderer.swapchain_rtv.Get(), color);
  renderer.ctx->ClearDepthStencilView(renderer.dsv.Get(),
                                      D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL,
                                      1.0f, 0);
  renderer.ctx->OMSetRenderTargets(1, renderer.swapchain_rtv.GetAddressOf(),
                                   renderer.dsv.Get());

  g_scene.cam.aspect = (f32)renderer.swapchain_desc.BufferDesc.Width / (f32)renderer.swapchain_desc.BufferDesc.Height;
  render_scene(renderer, g_scene, { 0, 0 }, { renderer.swapchain_desc.BufferDesc.Width, renderer.swapchain_desc.BufferDesc.Height });

  ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
}

// Things to do next:
// * Material system mixing PBR and custom shaders. Refer to Unreal Engine ~2013 article.
// * Support for clustered shading.
// ** Frustum - light volume intersection.
// ** List of lights per sub-frustum.
// * Mesh import + load from/save to internal format.
// * LOD levels for meshes based on distance to camera.
// * Animations (skeletal/morph). Interpolation between keyframes. Constant, linear, cubic.
// * UI.
// * Scene load/save.
// ** lua state load/save.
// *** Function serialization: lua files declare functions/classes (but do not modify environment),
// *** register them via config files.
// * Component-system framework.
// * Editor/play modes.
// * Different AntiAliasing modes.