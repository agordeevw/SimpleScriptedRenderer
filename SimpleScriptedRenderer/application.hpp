#pragma once
#include "d3d11_renderer.hpp"
#include "my_assert.hpp"
#include "types.hpp"
#include "input.hpp"
#include "lua.hpp"

class application
{
public:
  application(SDL_Window* window);
  ~application();
  void main_loop();

private:
  void resize();
  void update(f64 delta_time);
  void fixed_update(f64 delta_time);
  void render();

  f64 seconds_per_frame = 1.0 / 60.0;
  f64 seconds_per_update = 1.0 / 60.0;
  SDL_Window* m_window;
  d3d11_renderer renderer;
  lua_State* lua;
  ::input m_input;
};