#pragma once
#include "types.hpp"
#include "my_assert.hpp"
#include "my_new.hpp"
#include "util.hpp"

namespace detail
{
class object_pool_base
{
public:
  object_pool_base(u32 capacity, u32 obj_size);
  ~object_pool_base();

  u32 size() const
  {
    return m_size;
  }

  u32 capacity() const
  {
    return m_capacity;
  }

  void* alloc_helper();
  void free_helper(void* ptr);

protected:
  bool is_allocated(u32 slot_idx) const;
  void* object_at(u32 slot_idx) const;

private:
  inline u32 slot_size() const
  {
    return sizeof(u32) + m_obj_size;
  }

  // slot structure
  // 0: next_list_index, u32
  // 4: object storage, varies
  char* m_data;
  u32 m_size;
  u32 m_free_slot;
  u32 const m_capacity;
  u32 const m_obj_size;
};
} // namespace detail

template <class T>
class object_pool : private detail::object_pool_base
{
  using base_type = detail::object_pool_base;

public:
  object_pool(u32 capacity) : object_pool_base{ capacity, sizeof(T) }
  {
  }

  ~object_pool()
  {
    const u32 capacity = static_cast<base_type const&>(*this).capacity();
    for (u32 i = 0; i < capacity; i++)
      if (is_allocated(i))
      {
        reinterpret_cast<T*>(object_at(i))->~T();
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

  template <class ... Args>
  T* construct(Args&& ... args)
  {
    return new(alloc_helper(), placement_new) T{ util::forward<Args>(args)... };
  }

  void destroy(T* ptr)
  {
    // invoking free_helper before destructor allows to check that the pointer
    // is valid, but doesn't touch the actual memory
    free_helper(ptr);
    ptr->~T();
  }
};