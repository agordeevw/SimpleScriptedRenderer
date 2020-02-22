#pragma once
#include "my_new.hpp"
#include "types.hpp"

namespace detail
{
class ring_buffer_base
{
public:
  ring_buffer_base(u32 capacity, u32 obj_size);
  ~ring_buffer_base();

  u32 size() const
  {
    return m_size;
  }

  u32 capacity() const
  {
    return m_capacity;
  }

  void* push_back_helper();
  void* push_front_helper();
  void* pop_back_helper();
  void* pop_front_helper();
  void const* at(u32 idx) const;
  void* at(u32 idx);

private:
  char* m_data;
  u32 m_head = 0;
  u32 m_size = 0;
  u32 const m_capacity;
  u32 const m_obj_size;
};
} // namespace detail

template <class T>
class ring_buffer : private detail::ring_buffer_base
{
public:
  ring_buffer(u32 capacity) : ring_buffer_base{ capacity, sizeof(T) }
  {
  }

  u32 size() const
  {
    return static_cast<detail::ring_buffer_base const&>(*this).size();
  }

  u32 capacity() const
  {
    return static_cast<detail::ring_buffer_base const&>(*this).capacity();
  }

  void clear()
  {
    while (size() > 0)
    {
      pop_back();
    }
  }

  void push_back(T const& v)
  {
    new(push_back_helper(), my_operator_new) T{ v };
  }

  void push_front(T const& v)
  {
    new(push_front_helper(), my_operator_new) T{ v };
  }

  void pop_back()
  {
    reinterpret_cast<T*>(pop_back_helper())->~T();
  }

  void pop_front()
  {
    reinterpret_cast<T*>(pop_front_helper())->~T();
  }

  T const& operator[](u32 idx) const
  {
    return *reinterpret_cast<T const*>(at(idx));
  }

  T& operator[](u32 idx)
  {
    return *reinterpret_cast<T*>(at(idx));
  }
};