#pragma once
#include "my_assert.hpp"
#include "types.hpp"

template <class T, u32 t_capacity>
class static_vector
{
public:
  using iterator = T * ;
  using const_iterator = T const*;

  static_vector() : m_size(0)
  {
  }

  static_vector(T const* range_begin, T const* range_end) : static_vector()
  {
    while (range_begin < range_end)
      push_back(*range_begin++);
  }

  static_vector(T const* span_begin, u32 span_size) : static_vector(span_begin, span_begin + span_size)
  {
  }

  static_vector(static_vector const& other) : static_vector(reinterpret_cast<T const*>(other.m_data), reinterpret_cast<T const*>(other.m_data) + other.m_size)
  {
  }

  static_vector& operator=(static_vector const& other)
  {
    if (this != &other)
    {
      clear();
      for (u32 i = 0; i < other.m_size; i++)
        push_back(other[i]);
    }
    return *this;
  }

  ~static_vector()
  {
    clear();
  }

  T& operator[](u32 idx)
  {
    my_assert(idx < m_size);
    return reinterpret_cast<T*>(m_data)[idx];
  }

  T const& operator[](u32 idx) const
  {
    my_assert(idx < m_size);
    return reinterpret_cast<const T*>(m_data)[idx];
  }

  T& front()
  {
    my_assert(m_size > 0);
    return *reinterpret_cast<T*>(m_data);
  }

  T const& front() const
  {
    my_assert(m_size > 0);
    return *reinterpret_cast<const T*>(m_data);
  }

  T& back()
  {
    my_assert(m_size > 0);
    return reinterpret_cast<T*>(m_data)[m_size - 1];
  }

  T const& back() const
  {
    my_assert(m_size > 0);
    return reinterpret_cast<const T*>(m_data)[m_size - 1];
  }

  T* data()
  {
    return reinterpret_cast<T*>(m_data);
  }

  T const* data() const
  {
    return reinterpret_cast<const T*>(m_data);
  }

  iterator begin()
  {
    return reinterpret_cast<T*>(m_data);
  }

  const_iterator begin() const
  {
    return reinterpret_cast<const T*>(m_data);
  }

  const_iterator cbegin() const
  {
    return reinterpret_cast<const T*>(m_data);
  }

  iterator end()
  {
    return reinterpret_cast<T*>(m_data) + m_size;
  }

  const_iterator end() const
  {
    return reinterpret_cast<const T*>(m_data) + m_size;
  }

  const_iterator cend() const
  {
    return reinterpret_cast<const T*>(m_data) + m_size;
  }

  u32 size() const
  {
    return m_size;
  }

  u32 capacity() const
  {
    return t_capacity;
  }

  void clear()
  {
    for (u32 i = 0; i < m_size; i++)
    {
      reinterpret_cast<T*>(m_data)[i].~T();
    }
    m_size = 0;
  }

  void push_back(T const& value)
  {
    my_assert(m_size < t_capacity);
    reinterpret_cast<T*>(m_data)[m_size] = value;
    m_size++;
  }

  void push_back(T&& value)
  {
    my_assert(m_size < t_capacity);
    reinterpret_cast<T*>(m_data)[m_size] = value;
    m_size++;
  }

  void pop_back()
  {
    my_assert(m_size > 0);
    reinterpret_cast<T*>(m_data)[m_size - 1].~T();
    m_size--;
  }

  void resize(u32 new_size, T const& value = T{})
  {
    my_assert(new_size <= t_capacity);
    while (new_size > m_size)
      push_back(value);
    while (m_size > new_size)
      pop_back();
  }
private:
  char m_data[sizeof(T) * t_capacity];
  u32 m_size;
};