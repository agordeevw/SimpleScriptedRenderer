#include <SDL.h>
#include <SDL_syswm.h>

#include "application.hpp"
#include "imgui.h"
#include "imgui_impl_sdl.h"
#include "imgui_impl_dx11.h"
#include "ring_buffer.hpp"
#include "static_string.hpp"

static ring_buffer<static_string<512>> g_console_log{ 16 };

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

  lua = luaL_newstate();
  lua_pushglobaltable(lua);
  lua_pushcfunction(lua, print);
  lua_setfield(lua, -2, "print");
  lua_pop(lua, 1);
}

application::~application()
{
  lua_close(lua);

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
  ImGuiIO& io = ImGui::GetIO();

  ImGui::SetNextWindowPos({ 0, 0 });
  ImGui::Begin("Debug", nullptr,
               ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
  ImGui::Text("Frame time: %5.2lf ms", delta_time * 1000.0);
  ImGui::End();

  ImGui::SetNextWindowPos({ 0,100 });
  ImGui::SetNextWindowSize({ (f32)renderer.swapchain_width, 250 });
  ImGui::Begin("LUA state", nullptr,
               ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
  {
    for (int i = 0; i < g_console_log.size(); i++)
    {
      ImGui::TextWrapped("%s", g_console_log[i].c_str());
    }
  }
  ImGui::End();

  ImGui::SetNextWindowPos({ 0, 350 });
  ImGui::SetNextWindowSize({ (f32)renderer.swapchain_width, 200 });
  ImGui::Begin("Input", nullptr,
               ImGuiWindowFlags_NoResize | ImGuiWindowFlags_NoMove | ImGuiWindowFlags_NoCollapse);
  ImGui::SetKeyboardFocusHere();
  char input_buffer[512];
  input_buffer[0] = 0;
  if (ImGui::InputText("", input_buffer, sizeof(input_buffer), ImGuiInputTextFlags_EnterReturnsTrue, 0, this))
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
  ImGui::End();
}

void application::fixed_update(f64 delta_time)
{
  (void)delta_time;
}

void application::render()
{
  renderer.ctx->OMSetRenderTargets(1,
                                   renderer.swapchain_rtv.GetAddressOf(),
                                   renderer.dsv.Get());

  f32 color[] = { 0.0f, 0.0f, 0.0f, 1.0f };
  renderer.ctx->ClearRenderTargetView(renderer.swapchain_rtv.Get(), color);
  renderer.ctx->ClearDepthStencilView(renderer.dsv.Get(),
                                      D3D11_CLEAR_DEPTH | D3D11_CLEAR_STENCIL,
                                      1.0f, 0);
}
