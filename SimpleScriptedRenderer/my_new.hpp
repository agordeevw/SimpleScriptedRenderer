#pragma once
#include "types.hpp"

static struct placement_new_tag
{} placement_new;

inline void* operator new(u64, void* place, placement_new_tag)
{
  return place;
}
inline void* operator new[](u64, void* place, placement_new_tag)
{
  return place;
}

inline void operator delete(void* ptr, void* place, placement_new_tag) noexcept
{
  (void)ptr;
  (void)place;
}
inline void operator delete[](void* ptr, void* place, placement_new_tag) noexcept
{
  (void)ptr;
  (void)place;
}
