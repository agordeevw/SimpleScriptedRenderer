#pragma once
#include "types.hpp"

static struct placement_new_tag{} placement_new;

void* operator new(u64, void* place, placement_new_tag);
void* operator new[](u64, void* place, placement_new_tag);

void operator delete(void* ptr, void* place, placement_new_tag) noexcept;
void operator delete[](void* ptr, void* place, placement_new_tag) noexcept;
