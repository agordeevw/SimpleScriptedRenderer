#pragma once
#include "types.hpp"

static struct my_operator_new_tag{} my_operator_new;

void* operator new(u64, void* place, my_operator_new_tag);
void* operator new[](u64, void* place, my_operator_new_tag);

void operator delete(void* ptr, void* place, my_operator_new_tag) noexcept;
void operator delete[](void* ptr, void* place, my_operator_new_tag) noexcept;
