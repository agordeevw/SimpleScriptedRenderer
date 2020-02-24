#include <SDL.h>
#include <SDL_syswm.h>
#pragma warning(push)
#pragma warning(disable: 4201)
#define GLM_FORCE_DEPTH_ZERO_TO_ONE
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
#pragma warning(pop)

#include "application.hpp"
#include "imgui.h"
#include "imgui_impl_sdl.h"
#include "imgui_impl_dx11.h"
#include "ring_buffer.hpp"
#include "static_string.hpp"
#include "my_vector.hpp"
#include "static_vector.hpp"
#include "shader_bytecodes.h"
#include "pool.hpp"

static ring_buffer<static_string<512>> g_console_log{ 32 };
static bool g_console_enabled = false;

static void console_interpret(lua_State* lua, const char* input_buffer)
{
  {
    char console_buffer[1024];
    sprintf(console_buffer, "> %s", input_buffer);
    if (g_console_log.size() == g_console_log.capacity()) g_console_log.pop_front();
    g_console_log.push_back({ console_buffer });
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
      char report_buffer[512];
      sprintf(report_buffer, "error: %s", lua_tostring(lua, -1));
      if (g_console_log.size() == g_console_log.capacity()) g_console_log.pop_front();
      g_console_log.push_back({ report_buffer });
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

static int print(lua_State* lua)
{
  char text_buffer[512];
  text_buffer[0] = 0;
  char* text = text_buffer;
  const int nargs = lua_gettop(lua);
  for (int i = 1; i <= nargs; i++)
  {
    const char *s = luaL_tolstring(lua, i, nullptr);
    my_assert(s);
    if (i > 1) text += sprintf(text, "\t");
    text += sprintf(text, "%s", s);
    lua_pop(lua, 1);
  }

  if (g_console_log.size() == g_console_log.capacity()) g_console_log.pop_front();
  g_console_log.push_back({ text_buffer });
  return 0;
}

struct vertex_data
{
  com_ptr<ID3D11Buffer> data;
  u32 index_data_offset;
  u32 index_count;
};

struct vertex
{
  glm::vec3 position;
  glm::vec3 normal;
};

static vertex_data create_vertex_data(d3d11_renderer& renderer, my_vector<vertex> const& vertices, my_vector<u32> const& indices)
{
  vertex_data ret;

  my_vector<char> buffer_data;
  buffer_data.resize(vertices.size() * sizeof(vertices[0]) + indices.size() * sizeof(indices[0]));
  memcpy(buffer_data.data(), vertices.data(), vertices.size() * sizeof(vertices[0]));
  memcpy(buffer_data.data() + vertices.size() * sizeof(vertices[0]), indices.data(), indices.size() * sizeof(indices[0]));
  ret.index_data_offset = vertices.size() * sizeof(vertices[0]);
  ret.index_count = indices.size();

  {
    HRESULT hr;

    D3D11_BUFFER_DESC desc = {};
    desc.ByteWidth = buffer_data.size();
    desc.Usage = D3D11_USAGE_IMMUTABLE;
    desc.BindFlags = D3D11_BIND_VERTEX_BUFFER | D3D11_BIND_INDEX_BUFFER;

    D3D11_SUBRESOURCE_DATA initial_data = {};
    initial_data.pSysMem = buffer_data.data();
    hr = renderer.device->CreateBuffer(&desc, &initial_data, ret.data.ReleaseAndGetAddressOf());
    my_assert(SUCCEEDED(hr));
  }

  return ret;
}

static my_vector<vertex_data> vds = {};

static void create_vds(d3d11_renderer& renderer)
{
  // A triangle
  {
    my_vector<vertex> verts;
    my_vector<u32> indices;

    verts.push_back({ {-1.0f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f} });
    verts.push_back({ {0.0f, 0.5f, 0.0f}, {0.0f, 0.0f, 1.0f} });
    verts.push_back({ {1.0f, -0.5f, 0.0f}, {0.0f, 0.0f, 1.0f} });
    indices.push_back(0);
    indices.push_back(1);
    indices.push_back(2);

    vds.push_back(create_vertex_data(renderer, verts, indices));
  }
}

static void destroy_vds()
{
  vds = my_vector<vertex_data>{};
}

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

struct object_constants
{
  glm::mat4x4 local_to_world;
  glm::mat4x4 world_to_local_transposed;
  glm::vec3 object_color;
  f32 _pad0;
};

static scene_constants g_scene_constants;
static object_constants g_object_constants;

static com_ptr<ID3D11VertexShader> g_vs;
static com_ptr<ID3D11PixelShader> g_ps;
static com_ptr<ID3D11InputLayout> g_input_layout;
static com_ptr<ID3D11Buffer> g_buf_scene_constants;
static com_ptr<ID3D11Buffer> g_buf_object_constants;
static com_ptr<ID3D11BlendState> g_blend_state;
static com_ptr<ID3D11RasterizerState> g_rasterizer_state;
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
    hr = renderer.device->CreateRasterizerState(&desc, g_rasterizer_state.ReleaseAndGetAddressOf());
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
  g_rasterizer_state.Reset();
  g_blend_state.Reset();
  g_buf_object_constants.Reset();
  g_buf_scene_constants.Reset();
  g_input_layout.Reset();
  g_ps.Reset();
  g_vs.Reset();
}

struct transform
{
  glm::vec3 t = {};
  glm::quat r = glm::identity<glm::quat>();

  glm::mat4x4 local_to_world() const
  {
    glm::mat4x4 m;
    m[0] = 2.0f * glm::vec4{ r.x * r.x + r.w * r.w - 0.5f, r.x * r.y + r.z * r.w, r.x * r.z - r.y * r.w, 0.0f };
    m[1] = 2.0f * glm::vec4{ r.y * r.x - r.z * r.w, r.y * r.y + r.w * r.w - 0.5f, r.y * r.z - r.x * r.w, 0.0f };
    m[2] = 2.0f * glm::vec4{ r.z * r.x + r.y * r.w, r.z * r.y - r.x * r.w, r.z * r.z + r.w * r.w - 0.5f, 0.0f };
    m[3] = { t.x, t.y, t.z, 1.0f };
    return m;
  }

  glm::mat4x4 world_to_local_transposed() const
  {
    glm::mat4x4 m;
    m[0] = 2.0f * glm::vec4{ r.x * r.x + r.w * r.w - 0.5f, r.x * r.y + r.z * r.w, r.x * r.z - r.y * r.w, 0.0f };
    m[1] = 2.0f * glm::vec4{ r.y * r.x - r.z * r.w, r.y * r.y + r.w * r.w - 0.5f, r.y * r.z - r.x * r.w, 0.0f };
    m[2] = 2.0f * glm::vec4{ r.z * r.x + r.y * r.w, r.z * r.y - r.x * r.w, r.z * r.z + r.w * r.w - 0.5f, 0.0f };
    m[0].w = -t.x;
    m[1].w = -t.y;
    m[2].w = -t.z;
    m[3] = { 0.0f, 0.0f, 0.0f, 1.0f };
    return m;
  }
};

struct camera
{
  transform tr = {};
  float fov_degrees = 45.0f;
  float z_near = 0.1f;
  float z_far = 80.0f;
  float aspect = 1.0f;

  glm::mat4x4 world_to_screen() const
  {
    const glm::vec3& t = tr.t;
    const glm::quat& r = tr.r;

    glm::mat4x4 m;
    m[0] = 2.0f * glm::vec4{ r.x * r.x + r.w * r.w - 0.5f, r.x * r.y - r.z * r.w, r.x * r.z + r.y * r.w, 0.0f };
    m[1] = 2.0f * glm::vec4{ r.y * r.x + r.z * r.w, r.y * r.y + r.w * r.w - 0.5f, r.y * r.z + r.x * r.w, 0.0f };
    m[2] = 2.0f * glm::vec4{ r.z * r.x - r.y * r.w, r.z * r.y + r.x * r.w, r.z * r.z + r.w * r.w - 0.5f, 0.0f };
    m[3] = { 
      -(m[0][0] * t.x + m[1][0] * t.y + m[2][0] * t.z),
      -(m[0][1] * t.x + m[1][1] * t.y + m[2][1] * t.z),
      -(m[0][2] * t.x + m[1][2] * t.y + m[2][2] * t.z),
      1.0f };

    const f32 s = 1.0f / tan(glm::radians(fov_degrees) * 0.5f);
    const f32 f = z_far / (z_near - z_far);
    glm::mat4x4 p = {};
    p[0][0] = s / aspect;
    p[1][1] = s;
    p[2][2] = f;
    p[2][3] = -1.0f;
    p[3][2] = z_near * f;
    return p * m;
  }
};

struct entity
{
  static_string<32> name = {};
  transform tr = {};
  vertex_data vd;
  glm::vec3 color = { 0.5f, 0.8f, 0.5f };
  entity* parent = nullptr;
  static_vector<entity*, 4> children = {};
};

struct scene
{
  glm::vec3 light_dir = glm::normalize(glm::vec3{ 1.0f, 1.0f, 1.0f });
  glm::vec3 light_color = glm::vec3{ 1.0f, 1.0f, 1.0f };
  glm::vec3 ambient_color = glm::vec3{ 0.2f, 0.2f, 0.2f };
  camera cam = {};
  my_vector<entity*> entities;
  pool<entity> entity_pool = { 256 };
};

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

  for (u32 i = 0; i < sc.entities.size(); i++)
  {
    entity* e = sc.entities[i];
    transform tr = e->tr;
    {
      entity* pe = e->parent;
      while (pe)
      {
        tr.r = pe->tr.r * tr.r;
        tr.t = pe->tr.t + pe->tr.r * tr.t;
      }
    }
    g_object_constants.local_to_world = tr.local_to_world();
    g_object_constants.world_to_local_transposed = tr.world_to_local_transposed();
    g_object_constants.object_color = e->color;
    {
      D3D11_MAPPED_SUBRESOURCE mapped;
      renderer.ctx->Map(g_buf_object_constants.Get(), 0, D3D11_MAP_WRITE_DISCARD, 0, &mapped);
      memcpy(mapped.pData, &g_object_constants, sizeof(g_object_constants));
      renderer.ctx->Unmap(g_buf_object_constants.Get(), 0);
    }
    u32 stride = sizeof(vertex);
    u32 offset = 0;
    renderer.ctx->IASetVertexBuffers(0, 1, e->vd.data.GetAddressOf(), &stride, &offset);
    renderer.ctx->IASetIndexBuffer(e->vd.data.Get(), DXGI_FORMAT_R32_UINT, e->vd.index_data_offset);
    renderer.ctx->IASetInputLayout(g_input_layout.Get());
    renderer.ctx->IASetPrimitiveTopology(D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
    renderer.ctx->VSSetShader(g_vs.Get(), nullptr, 0);
    renderer.ctx->VSSetConstantBuffers(0, 1, g_buf_scene_constants.GetAddressOf());
    renderer.ctx->VSSetConstantBuffers(1, 1, g_buf_object_constants.GetAddressOf());
    renderer.ctx->PSSetShader(g_ps.Get(), nullptr, 0);
    renderer.ctx->PSSetConstantBuffers(0, 1, g_buf_scene_constants.GetAddressOf());
    renderer.ctx->PSSetConstantBuffers(1, 1, g_buf_object_constants.GetAddressOf());
    renderer.ctx->RSSetState(g_rasterizer_state.Get());
    D3D11_VIEWPORT vp = {};
    vp.MinDepth = 0.0f;
    vp.MaxDepth = 1.0f;
    vp.TopLeftX = viewport_pos.x;
    vp.TopLeftY = viewport_pos.y;
    vp.Width = viewport_size.x;
    vp.Height = viewport_size.y;
    renderer.ctx->RSSetViewports(1, &vp);
    renderer.ctx->DrawIndexed(e->vd.index_count, 0, 0);
  }
}

static scene g_scene = {};

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

  lua = luaL_newstate();
  lua_pushglobaltable(lua);
  lua_pushcfunction(lua, print);
  lua_setfield(lua, -2, "print");
  lua_pop(lua, 1);

  g_scene.cam.tr.t = { 0.0f, 0.0f, 7.0f };
  {
    g_scene.entities.push_back(g_scene.entity_pool.alloc());
    g_scene.entities.back()->vd = vds[0];
    g_scene.entities.back()->tr.t = { 2.0f, 0.0f, 0.0f };
    g_scene.entities.back()->color = { 1.0f, 0.0f, 0.0f };
  }
  {
    g_scene.entities.push_back(g_scene.entity_pool.alloc());
    g_scene.entities.back()->vd = vds[0];
    g_scene.entities.back()->tr.t = { 1.5f, 1.5f, 0.0f };
    g_scene.entities.back()->tr.r =
      glm::angleAxis(-3.141592f * 0.25f, glm::normalize(glm::vec3{ 1.0f, 0.0f, 0.0f }));
    g_scene.entities.back()->color = { 1.0f, 0.0f, 0.0f };
  }
  {
    g_scene.entities.push_back(g_scene.entity_pool.alloc());
    g_scene.entities.back()->vd = vds[0];
    g_scene.entities.back()->tr.t = { 0.0f, 2.0f, 0.0f };
    g_scene.entities.back()->color = { 0.0f, 1.0f, 0.0f };
  }
  {
    g_scene.entities.push_back(g_scene.entity_pool.alloc());
    g_scene.entities.back()->vd = vds[0];
    g_scene.entities.back()->tr.t = { 0.0f, 0.0f, 2.0f };
    g_scene.entities.back()->color = { 0.0f, 0.0f, 1.0f };
  }
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

    SDL_Event event;
    while (SDL_PollEvent(&event))
    {
      ImGui_ImplSDL2_ProcessEvent(&event);
      if (event.type == SDL_QUIT)
      {
        loop_active = false;
      }
      else if (event.type == SDL_WINDOWEVENT)
      {
        if (event.window.event == SDL_WINDOWEVENT_RESIZED)
        {
          resize();
        }
      }
    }

    int updates_left = 2;
    while (lag >= seconds_per_update)
    {
      fixed_update(seconds_per_update);
      lag -= seconds_per_update;
      updates_left--;
      if (updates_left == 0) break;
    }

    {
      ImGui_ImplDX11_NewFrame();
      ImGui_ImplSDL2_NewFrame(m_window);
      ImGui::NewFrame();
      update(delta_time);
    }

    {
      ImGui::Render();
      render();
      ImGui_ImplDX11_RenderDrawData(ImGui::GetDrawData());
    }

    constexpr bool d3d11_vsync = true;
    if (d3d11_vsync)
    {
      renderer.swapchain->Present(1, 0);
    }
    else
    {
      if (elapsed_time < seconds_per_frame)
      {
        u32 delay_ms = (u32)((seconds_per_frame - elapsed_time) * 1000.0);
        SDL_Delay(delay_ms);
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

// Draw ImGUI here.
void application::update(f64 delta_time)
{
  g_scene.entities[0]->tr.r *=
    glm::angleAxis(4.0f * (f32)delta_time, glm::vec3{ 0.0f, 0.0f, 1.0f });
  g_scene.entities[1]->tr.r *=
    glm::angleAxis(4.0f * (f32)delta_time, glm::normalize(glm::vec3{ 0.0f, 0.0f, 1.0f }));

  if (m_input.key_pressed(SDL_SCANCODE_GRAVE))
  {
    g_console_enabled = !g_console_enabled;
  }

  ImGuiIO& io = ImGui::GetIO();

  ImGui::SetNextWindowPos({ 0, 0 });
  ImGui::Begin("Debug", nullptr,
               ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
  ImGui::Text("Frame time: %5.2lf ms", delta_time * 1000.0);
  ImGui::End();

  ImGui::Begin("Q", nullptr);
  ImGui::Text("Q: %.3f %.3f %.3f %.3f",
              g_scene.entities[0]->tr.r[0],
              g_scene.entities[0]->tr.r[1],
              g_scene.entities[0]->tr.r[2],
              g_scene.entities[0]->tr.r[3]);
  ImGui::End();

  if (g_console_enabled)
  {
    ImGui::SetNextWindowPos({ 0, (f32)renderer.swapchain_height - 300 });
    ImGui::SetNextWindowSize({ 600, 300 });
    if (ImGui::Begin("Console", nullptr,
                     ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse))
    {
      ImGui::SetKeyboardFocusHere();
      if (ImGui::BeginChild("Console log", { 0, -30 }), true)
      {
        for (int i = 0; i < g_console_log.size(); i++)
          ImGui::TextWrapped("%s", g_console_log[i].c_str());
      }
      ImGui::EndChild();
      char input_buffer[512];
      input_buffer[0] = 0;
      if (ImGui::InputText("", input_buffer, sizeof(input_buffer), ImGuiInputTextFlags_EnterReturnsTrue, 0, this))
      {
        console_interpret(lua, input_buffer);
      }
    }
    ImGui::End();
  }
}

void application::fixed_update(f64 delta_time)
{
  (void)delta_time;
}

void application::render()
{
  f32 color[] = { 0.0f, 0.0f, 0.0f, 1.0f };
  renderer.ctx->ClearRenderTargetView(renderer.swapchain_rtv.Get(), color);
  renderer.ctx->ClearDepthStencilView(renderer.dsv.Get(),
                                      D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL,
                                      1.0f, 0);

  renderer.ctx->OMSetRenderTargets(1,
                                   renderer.swapchain_rtv.GetAddressOf(),
                                   renderer.dsv.Get());

  g_scene.cam.aspect = (f32)renderer.swapchain_width / (f32)renderer.swapchain_height;
  render_scene(renderer, g_scene,
               { 0, 0 }, { renderer.swapchain_width, renderer.swapchain_height });
}
