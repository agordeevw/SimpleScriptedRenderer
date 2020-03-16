#include "my_assert.hpp"
#include "object_pool.hpp"
#include "types.hpp"
#include "util.hpp"

detail::object_pool_base::object_pool_base(u32 capacity, u32 obj_size) : m_capacity{ capacity }, m_obj_size{ obj_size }
{
  m_data = new char[slot_size() * capacity];
  for (u32 i = 0; i < m_capacity; i++)
    *reinterpret_cast<u32*>(m_data + slot_size() * i) = i + 1;
  m_size = 0;
  m_free_slot = 0;
}

detail::object_pool_base::~object_pool_base()
{
  delete[] m_data;
}

void* detail::object_pool_base::alloc_helper()
{
  my_assert(m_size < m_capacity);
  m_size++;
  char* slot = m_data + m_free_slot * slot_size();
  m_free_slot = *reinterpret_cast<u32*>(slot);
  *reinterpret_cast<u32*>(slot) = (u32)-1;
  return slot + sizeof(u32);
}

void detail::object_pool_base::free_helper(void* ptr)
{
  my_assert(m_size > 0);
  char* slot = reinterpret_cast<char*>(ptr) - sizeof(u32);
  my_assert(slot >= m_data);
  my_assert(slot < m_data + slot_size() * m_capacity);
  const u32 slot_idx = (u32)((slot - m_data) / slot_size());
  my_assert(slot == m_data + m_obj_size * slot_idx);

  m_size--;
  *reinterpret_cast<u32*>(slot) = m_free_slot;
  m_free_slot = slot_idx;
}

bool detail::object_pool_base::is_allocated(u32 slot_idx) const
{
  my_assert(slot_idx < m_capacity);
  char* slot = m_data + slot_idx * slot_size();
  return *reinterpret_cast<u32*>(slot) == (u32)-1;
}

void* detail::object_pool_base::object_at(u32 slot_idx) const
{
  my_assert(slot_idx < m_capacity);
  char* slot = m_data + slot_idx * slot_size();
  return slot + sizeof(u32);
}
