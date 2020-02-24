#pragma once
#include "types.hpp"
#include "my_assert.hpp"
#include "my_new.hpp"

namespace detail
{
class pool_base
{
public:
  // slot structure
  // 0: next_list_index, u32
  // 4: object storage, varies

  pool_base(u32 capacity, u32 obj_size) : m_capacity{ capacity }, m_obj_size{ obj_size }
  {
    m_data = new char[slot_size() * capacity];
    for (u32 i = 0; i < m_capacity; i++)
      *reinterpret_cast<u32*>(m_data + slot_size() * i) = i + 1;
    m_size = 0;
    m_free_slot = 0;
  }

  ~pool_base()
  {
    delete[] m_data;
  }

  u32 size() const
  {
    return m_size;
  }

  u32 capacity() const
  {
    return m_capacity;
  }

  void* alloc_helper()
  {
    my_assert(m_size < m_capacity);
    char* slot = m_data + m_free_slot * slot_size();
    m_free_slot = *reinterpret_cast<u32*>(slot);
    *reinterpret_cast<u32*>(slot) = (u32)-1;
    return slot + sizeof(u32);
  }

  void free_helper(void* ptr)
  {
    my_assert(m_size > 0);
    char* slot = reinterpret_cast<char*>(ptr) - sizeof(u32);
    my_assert(slot >= m_data);
    my_assert(slot < m_data + slot_size() * m_capacity);
    const u32 slot_idx = (u32)((slot - m_data) / slot_size());
    my_assert(slot == m_data + m_obj_size * slot_idx);

    *reinterpret_cast<u32*>(slot) = m_free_slot;
    m_free_slot = slot_idx;
  }

protected:
  bool is_allocated(u32 slot_idx) const
  {
    my_assert(slot_idx < m_capacity);
    char* slot = m_data + slot_idx * slot_size();
    return *reinterpret_cast<u32*>(slot) == (u32)-1;
  }

  void* object_at(u32 slot_idx) const
  {
    my_assert(slot_idx < m_capacity);
    char* slot = m_data + slot_idx * slot_size();
    return slot + sizeof(u32);
  }

private:
  inline u32 slot_size() const
  {
    return sizeof(u32) + m_obj_size;
  }

  char* m_data;
  u32 m_size;
  u32 m_free_slot;
  u32 const m_capacity;
  u32 const m_obj_size;
};
} // namespace detail

template <class T>
class pool : private detail::pool_base
{
  using base_type = detail::pool_base;

public:
  pool(u32 capacity) : pool_base{ capacity, sizeof(T) }
  {
  }

  ~pool()
  {
    const u32 capacity = static_cast<base_type const&>(*this).capacity();
    for (u32 i = 0; i < capacity; i++)
      if (is_allocated(i))
      {
        T* ptr = reinterpret_cast<T*>(object_at(i));
        ptr->~T();
      }
  }

  u32 size() const
  {
    return static_cast<base_type const&>(*this).size();
  }

  u32 capacity() const
  {
    return static_cast<base_type const&>(*this).capacity();
  }

  T* alloc()
  {
    return new(alloc_helper(), my_operator_new) T{};
  }

  T* alloc(const T& value)
  {
    return new(alloc_helper(), my_operator_new) T{ value };
  }

  void free(T* ptr)
  {
    // invoking free_helper before destructor allows to check that the pointer
    // is valid, but doesn't touch the actual memory
    free_helper(ptr);
    ptr->~T();
  }
};