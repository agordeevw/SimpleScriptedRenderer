#pragma once
#include "my_assert.hpp"
#include "my_new.hpp"
#include "types.hpp"
#include "util.hpp"

template <class T>
class vector
{
public:
  using iterator = T * ;
  using const_iterator = T const*;

  vector() : m_data(nullptr), m_size(0), m_capacity(0)
  {
  }

  vector(T const* range_begin, T const* range_end) : vector()
  {
    my_assert(range_begin <= range_end);
    if (range_end != range_begin)
    {
      m_capacity = (u32)(((u32)(range_end - range_begin) * 125) / 100);
      m_data = new char[sizeof(T) * m_capacity];
      while (range_begin < range_end)
        push_back(*range_begin++);
    }
  }

  vector(T const* span_begin, u32 span_size) : vector(span_begin,
                                                      span_begin + span_size)
  {
  }

  vector(vector const& other) : vector(reinterpret_cast<T const*>(other.m_data),
                                       reinterpret_cast<T const*>(other.m_data) + other.m_size)
  {
  }

  vector(vector&& other) : vector()
  {
    swap(other);
  }

  vector& operator=(vector const& other)
  {
    if (this != &other)
    {
      vector tmp = other;
      swap(tmp);
    }
    return *this;
  }

  vector& operator=(vector&& other)
  {
    if (this != &other)
    {
      vector tmp = other;
      swap(tmp);
    }
    return *this;
  }

  ~vector()
  {
    clear();
    delete[] m_data;
    m_data = nullptr;
    m_capacity = 0;
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

  void reserve(u32 new_capacity)
  {
    if (new_capacity > m_capacity)
    {
      char* new_data = new char[sizeof(T) * new_capacity];
      u32 const new_size = m_size;
      for (u32 i = 0; i < new_size; i++)
      {
        T* new_addr = &reinterpret_cast<T*>(new_data)[i];
        T* old_addr = &reinterpret_cast<T*>(m_data)[i];
        new(new_addr, placement_new) T{ util::move(*old_addr) };
      }
      this->~vector();
      m_data = new_data;
      m_size = new_size;
      m_capacity = new_capacity;
    }
  }

  u32 capacity() const
  {
    return m_capacity;
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
    if (m_size == m_capacity)
    {
      reserve((m_capacity + 1) * 2);
    }
    new(m_data + m_size * sizeof(T), placement_new) T{ value };
    m_size++;
  }

  void push_back(T&& value)
  {
    if (m_size == m_capacity)
    {
      reserve((m_capacity + 1) * 2);
    }
    new(m_data + m_size * sizeof(T), placement_new) T{ value };
    m_size++;
  }

  void pop_back()
  {
    back().~T();
    m_size--;
  }

  void resize(u32 new_size, T const& value)
  {
    if (new_size > m_capacity)
    {
      reserve((u32)((new_size * 125) / 100));
    }
    while (new_size > m_size)
    {
      push_back(value);
    }
    while (m_size > new_size)
    {
      pop_back();
    }
  }

  void swap(vector& other)
  {
    util::swap(m_data, other.m_data);
    util::swap(m_size, other.m_size);
    util::swap(m_capacity, other.m_capacity);
  }

private:
  char* m_data;
  u32 m_size;
  u32 m_capacity;
};
