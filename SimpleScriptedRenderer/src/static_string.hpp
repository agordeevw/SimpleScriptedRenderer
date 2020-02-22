#pragma once
#include "types.hpp"

namespace detail
{
class static_string_base
{
public:
  static_string_base(char* managed_string, u64 capacity);
  static_string_base(char* managed_string, u64 capacity, const char* str);
  static_string_base(static_string_base const&) = delete;
  static_string_base(static_string_base&&) = delete;

  char operator[](u32 i) const
  {
    return m_data[i];
  }

  char& operator[](u32 i)
  {
    return m_data[i];
  }

  const char* c_str() const
  {
    return m_data;
  }

  u64 capacity() const
  {
    return m_capacity;
  }

private:
  char* m_data;
  u64 m_capacity;
};
} // namespace detail


bool operator==(detail::static_string_base const& lhs, detail::static_string_base const& rhs);
bool operator==(const char* lhs, detail::static_string_base const& rhs);
bool operator==(detail::static_string_base const& lhs, const char* rhs);

template <u64 t_capacity>
class static_string : public detail::static_string_base
{
public:
  static_string() : detail::static_string_base{ data, t_capacity }
  {
  }

  static_string(const char* str) : detail::static_string_base{ data, t_capacity, str }
  {
  }

  static_string(static_string const& other) : detail::static_string_base{ data, t_capacity, other.data }
  {
  }

  static_string(static_string&& other) : detail::static_string_base{ data, t_capacity, other.data }
  {
  }

private:
  char data[t_capacity];
};