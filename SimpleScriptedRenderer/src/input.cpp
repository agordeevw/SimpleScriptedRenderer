#include <string.h>

#include "SDL.h"

#include "input.hpp"

input::input()
{
  static_assert(sizeof(input::m_prev_key_state) >= SDL_NUM_SCANCODES, "");
  m_key_state = SDL_GetKeyboardState(nullptr);
}

bool input::key_pressed(int scancode) const
{
  return m_key_state[scancode] && !m_prev_key_state[scancode];
}

bool input::key_released(int scancode) const
{
  return m_prev_key_state[scancode] && !m_key_state[scancode];
}

void input::update()
{
  memcpy(m_prev_key_state, m_key_state, SDL_NUM_SCANCODES);
}
