#include "my_assert.hpp"
#include "ring_buffer.hpp"

detail::ring_buffer_base::ring_buffer_base(u32 capacity, u32 obj_size)
  : m_capacity(capacity)
  , m_obj_size(obj_size)
{
  m_data = new char[capacity * obj_size];
}

detail::ring_buffer_base::~ring_buffer_base()
{
  delete[] m_data;
}

void* detail::ring_buffer_base::push_back_helper()
{
  my_assert(m_size < m_capacity);
  void* ret = at(m_size);
  m_size++;
  return ret;
}

void* detail::ring_buffer_base::push_front_helper()
{
  my_assert(m_size < m_capacity);
  void* ret = at(m_capacity - 1u);
  m_head = (m_head + m_capacity - 1u) % m_capacity;
  m_size++;
  return ret;
}

void* detail::ring_buffer_base::pop_back_helper()
{
  my_assert(m_size > 0);
  void* ret = at(m_size - 1u);
  m_size--;
  return ret;
}

void* detail::ring_buffer_base::pop_front_helper()
{
  my_assert(m_size > 0);
  void* ret = at(0);
  m_head = (m_head + 1) % m_capacity;
  m_size--;
  return ret;
}

void const * detail::ring_buffer_base::at(u32 idx) const
{
  return m_data + ((m_head + idx) % m_capacity) * m_obj_size;
}

void * detail::ring_buffer_base::at(u32 idx)
{
  return m_data + ((m_head + idx) % m_capacity) * m_obj_size;
}
