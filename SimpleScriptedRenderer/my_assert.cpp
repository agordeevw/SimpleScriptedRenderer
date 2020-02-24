#include <stdio.h>
#include <stdlib.h>
#include "my_assert.hpp"

void detail::my_assert_handler(const char* expr, const char* file, int line)
{
  fprintf(stderr, "Assertion failed: %s, in file %s at line %d", expr, file, line);
  abort();
}
