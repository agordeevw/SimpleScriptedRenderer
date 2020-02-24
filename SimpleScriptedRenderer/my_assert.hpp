#pragma once

namespace detail
{
  void my_assert_handler(const char* expr, const char* file, int line);
} // namespace detail

#ifdef _DEBUG
#define my_assert(x) \
do { if ((x) == false) { detail::my_assert_handler(#x, __FILE__, __LINE__); } } while(0);
#else
#define my_assert(x) \
do { (void)sizeof(x); } while(0);
#endif