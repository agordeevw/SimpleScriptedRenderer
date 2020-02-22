#pragma once
#include "types.hpp"

class input
{
public:
  input();

  bool key_pressed(int scancode) const;
  bool key_released(int scancode) const;
  void update();

private:
  u8 const* m_key_state;
  u8 m_prev_key_state[512];
};
