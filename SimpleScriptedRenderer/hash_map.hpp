#pragma once
#include "types.hpp"
#include "util.hpp"

template <class t_key, class t_value, class t_hasher = util::default_hasher<t_key>>
class hash_map
{
public:
  struct kv_pair
  {
    t_key key;
    t_value value;
  };

  class const_iter
  {
  public:
    using value_type = kv_pair const;
    using reference = kv_pair const&;

    const_iter(hash_map const* instance, u32 pos) : instance(instance), pos(pos)
    {}

    reference operator*() const
    {
      return instance->m_buffer[pos];
    }

    value_type* operator->() const
    {
      return &instance->m_buffer[pos];
    }

    const_iter& operator++()
    {
      u32 const capacity = instance->m_capacity;
      u32 const mask = instance->m_mask;
      while (++pos < capacity)
      {
        u32 const hash = instance->m_hashes[pos];
        if (instance->is_hash_alive(hash))
          break;
      }
      return *this;
    }

    const_iter operator++(int)
    {
      const_iter tmp = *this;
      this->operator++();
      return tmp;
    }

    bool operator==(const_iter const& other) const
    {
      return instance == other.instance && pos == other.pos;
    }

    bool operator!=(const_iter const& other) const
    {
      return instance != other.instance || pos != other.pos;
    }

  private:
    hash_map const* instance;
    u32 pos;
  };

  class iter : public const_iter
  {
  public:
    using value_type = kv_pair;
    using reference = kv_pair & ;

    iter(hash_map* instance, u32 pos) : const_iter(const_cast<hash_map const*>(instance), pos)
    {}

    reference operator*() const
    {
      return const_cast<reference>(static_cast<const_iter const&>(*this).operator*());
    }

    value_type* operator->() const
    {
      return const_cast<value_type*>(static_cast<const_iter const&>(*this).operator->());
    }

    iter& operator++()
    {
      static_cast<const_iter&>(*this).operator++();
      return *this;
    }

    iter operator++(int)
    {
      iter tmp = *this;
      this->operator++();
      return tmp;
    }

    bool operator==(iter const& other) const
    {
      return static_cast<const_iter const&>(*this) == static_cast<const_iter const&>(other);
    }

    bool operator!=(iter const& other) const
    {
      return static_cast<const_iter const&>(*this) != static_cast<const_iter const&>(other);
    }
  };

  using iterator = iter;
  using const_iterator = const iter;

  hash_map()
  {}

  hash_map(hash_map const& other)
  {
    alloc(other.m_capacity);
    // keys are hashed again, may want to use hashes stored in other map
    for (auto it = other.begin(); it != other.end(); ++it)
      insert(t_key{ it->key }, t_value{ it->value });
  }

  hash_map(hash_map&& other) : hash_map()
  {
    swap(other);
  }

  hash_map& operator=(hash_map const& other)
  {
    if (this != &other)
    {
      hash_map tmp(other);
      swap(tmp);
    }
    return *this;
  }

  hash_map& operator=(hash_map&& other)
  {
    if (this != &other)
    {
      hash_map tmp(other);
      swap(tmp);
    }
    return *this;
  }

  ~hash_map()
  {
    clear();
    delete[] m_hashes;
    free(m_buffer);
  }

  iter begin()
  {
    u32 pos = 0;
    while (pos < m_capacity)
    {
      if (is_hash_alive(m_hashes[pos]))
        break;
      pos++;
    }
    return iter{ this, pos };
  }

  iter end()
  {
    return iter{ this, m_capacity };
  }

  const_iter begin() const
  {
    return static_cast<const_iter>(const_cast<hash_map*>(this)->begin());
  }

  const_iter end() const
  {
    return static_cast<const_iter>(const_cast<hash_map*>(this)->end());
  }

  const_iter cbegin() const
  {
    return static_cast<const_iter>(const_cast<hash_map*>(this)->begin());
  }

  const_iter cend() const
  {
    return static_cast<const_iter>(const_cast<hash_map*>(this)->end());
  }

  u32 size() const
  {
    return m_size;
  }

  void clear()
  {
    m_num_tombstones = 0;
    m_size = 0;
    for (u32 i = 0; i < m_capacity; i++)
    {
      u32 hash = m_hashes[i];
      m_hashes[i] = 0;
      if (is_hash_alive(hash))
      {
        m_buffer[i].~kv_pair();
      }
    }
  }

  template <class K, class V>
  void insert(K&& key, V&& val)
  {
    ++m_size;
    if (m_size + m_num_tombstones >= m_resize_threshold)
    {
      u32 new_capacity = m_capacity;
      if (m_num_tombstones <= m_rehash_threshold)
      {
        new_capacity = m_capacity > 0 ? (2 * m_capacity) : INITIAL_CAPACITY;
      }
      realloc(new_capacity);
    }
    insert_for_hash(hash_key(key), t_key{ key }, t_value{ val });
  }

  bool erase(const t_key& key)
  {
    const u32 pos = find_pos_of_key(key);
    if (pos == m_capacity)
    {
      return false;
    }

    m_buffer[pos].~kv_pair();
    m_hashes[pos] |= 0x80000000;
    m_num_tombstones++;
    m_size--;
    return true;
  }

  void swap(hash_map& other)
  {
    util::swap(m_buffer, other.m_buffer);
    util::swap(m_hashes, other.m_hashes);
    util::swap(m_size, other.m_size);
    util::swap(m_capacity, other.m_capacity);
    util::swap(m_num_tombstones, other.m_num_tombstones);
    util::swap(m_resize_threshold, other.m_resize_threshold);
    util::swap(m_rehash_threshold, other.m_rehash_threshold);
    util::swap(m_mask, other.m_mask);
  }

  iterator find(const t_key& key)
  {
    return iterator{ this, find_pos_of_key(key) };
  }

  const_iterator find(const t_key& key) const
  {
    return static_cast<const_iterator>(const_cast<hash_map*>(this)->find(key));
  }

private:
  void alloc(u32 new_capacity)
  {
    if (new_capacity == 0)
    {
      return;
    }
    m_buffer = reinterpret_cast<kv_pair*>(malloc(new_capacity * sizeof(kv_pair)));
    my_assert(m_buffer);
    m_hashes = new u32[new_capacity];
    my_assert(m_hashes);
    for (u32 i = 0; i < new_capacity; i++)
    {
      m_hashes[i] = 0;
    }
    m_size = 0;
    m_capacity = new_capacity;
    m_num_tombstones = 0;
    m_resize_threshold = (m_capacity * MAX_LOAD_FACTOR) / 100;
    m_rehash_threshold = (m_capacity * MAX_TOMBSTONE_FACTOR) / 100;
    m_mask = m_capacity - 1;
  }

  void realloc(u32 new_capacity)
  {
    hash_map tmp = util::move(*this);
    alloc(new_capacity);
    m_size = tmp.m_size;

    u32 const capacity = tmp.m_capacity;
    for (u32 i = 0; i < capacity; i++)
    {
      kv_pair& e = tmp.m_buffer[i];
      u32 hash = tmp.m_hashes[i];
      if (is_hash_alive(hash))
      {
        insert_for_hash(hash, util::move(e.key), util::move(e.value));
      }
    }
  }

  static u32 hash_key(t_key const& k)
  {
    u32 h = static_cast<u32>(t_hasher{}(k));
    h &= 0x7FFFFFFF;
    h |= h == 0;
    return h;
  }

  inline static bool is_hash_deleted(u32 hash)
  {
    return (hash >> 31) != 0;
  }

  inline static bool is_hash_alive(u32 hash)
  {
    return hash && is_hash_deleted(hash) == false;
  }

  u32 desired_pos_for_hash(u32 hash) const
  {
    return hash & m_mask;
  }

  u32 probe_distance(u32 hash, u32 pos) const
  {
    return (pos + m_capacity - desired_pos_for_hash(hash)) & m_mask;
  }

  void insert_for_hash(u32 hash, t_key&& key, t_value&& val)
  {
    u32 pos = desired_pos_for_hash(hash);
    u32 dist = 0;
    for (;;)
    {
      if (m_hashes[pos] == 0)
      {
        break;
      }
      u32 existing_elem_probe_dist = probe_distance(m_hashes[pos], pos);
      if (existing_elem_probe_dist < dist)
      {
        if (is_hash_deleted(m_hashes[pos]))
        {
          m_num_tombstones--;
          break;
        }

        util::swap(hash, m_hashes[pos]);
        util::swap(key, m_buffer[pos].key);
        util::swap(val, m_buffer[pos].value);
        dist = existing_elem_probe_dist;
      }

      pos = (pos + 1) & m_mask;
      ++dist;
    }
    new (&m_buffer[pos], placement_new) kv_pair{ key, val };
    m_hashes[pos] = hash;
  }

  u32 find_pos_of_key(const t_key& key) const
  {
    if (m_capacity == 0)
    {
      return m_capacity;
    }

    const u32 hash = hash_key(key);
    u32 pos = desired_pos_for_hash(hash);
    u32 dist = 0;
    for (;;)
    {
      if (m_hashes[pos] == 0 || dist > probe_distance(m_hashes[pos], pos))
        return m_capacity;
      if (m_hashes[pos] == hash && m_buffer[pos].key == key)
        return pos;

      pos = (pos + 1) & m_mask;
      ++dist;
    }
  }

  static const u32 INITIAL_CAPACITY = 32;
  static const u32 MAX_LOAD_FACTOR = 80;
  static const u32 MAX_TOMBSTONE_FACTOR = 40;

  kv_pair* m_buffer = nullptr;
  u32* m_hashes = nullptr;
  u32 m_size = 0;
  u32 m_capacity = 0;
  u32 m_num_tombstones = 0;
  u32 m_resize_threshold = 0;
  u32 m_rehash_threshold = 0;
  u32 m_mask = 0;
};