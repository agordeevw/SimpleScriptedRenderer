#include "my_new.hpp"

void* operator new(u64, void* place, placement_new_tag)
{
  return place;
}

void* operator new[](u64, void* place, placement_new_tag)
{
  return place;
}

void operator delete(void*, void*, placement_new_tag) noexcept
{
}

void operator delete[](void*, void*, placement_new_tag) noexcept
{
}
