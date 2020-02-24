#include <string.h>
#include "my_assert.hpp"
#include "static_string.hpp"

detail::static_string_base::static_string_base(char* managed_string, u64 capacity) :
  m_data{ managed_string },
  m_capacity(capacity)
{
  my_assert(m_capacity > 0);
  m_data[0] = 0;
}

detail::static_string_base::static_string_base(char* managed_string, u64 capacity, const char* str) :
  static_string_base{ managed_string, capacity }
{
  u64 count = strlen(str) + 1;
  my_assert(count <= m_capacity);
  memcpy(m_data, str, count);
}

bool operator==(detail::static_string_base const& lhs, detail::static_string_base const& rhs)
{
  return strcmp(lhs.c_str(), rhs.c_str()) == 0;
}

bool operator==(const char* lhs, detail::static_string_base const& rhs)
{
  return strcmp(lhs, rhs.c_str()) == 0;
}

bool operator==(detail::static_string_base const& lhs, const char* rhs)
{
  return strcmp(lhs.c_str(), rhs) == 0;
}