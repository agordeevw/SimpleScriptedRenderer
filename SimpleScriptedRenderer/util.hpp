#pragma once
#include "types.hpp"

namespace util
{
template <class T>
struct remove_reference
{
  using type = T;
};

template <class T>
struct remove_reference<T&>
{
  using type = T;
};

template <class T>
struct remove_reference<T&&>
{
  using type = T;
};

template <class T>
inline constexpr auto move(T&& v)
{
  return static_cast<typename remove_reference<T>::type&&>(v);
}

template <class T>
inline constexpr T&& forward(typename remove_reference<T>::type& v)
{
  return static_cast<T&&>(v);
}

template <class T>
inline constexpr T&& forward(typename remove_reference<T>::type&& v)
{
  return static_cast<T&&>(v);
}

template <class T>
inline constexpr void swap(T& lhs, T& rhs)
{
  T tmp = move(lhs);
  lhs = move(rhs);
  rhs = move(tmp);
}

template <bool cond, class T = void>
struct enable_if
{};

template <class T>
struct enable_if<true, T>
{
  using type = T;
};

struct false_type
{
  static const bool value = false;
};

struct true_type
{
  static const bool value = true;
};

template <class T>
struct is_integral : false_type
{};

#define IS_INTEGRAL_TRAIT(type) \
template <> struct is_integral<type> : true_type {}

IS_INTEGRAL_TRAIT(i8);
IS_INTEGRAL_TRAIT(i16);
IS_INTEGRAL_TRAIT(i32);
IS_INTEGRAL_TRAIT(i64);
IS_INTEGRAL_TRAIT(u8);
IS_INTEGRAL_TRAIT(u16);
IS_INTEGRAL_TRAIT(u32);
IS_INTEGRAL_TRAIT(u64);

#undef IS_INTEGRAL_TRAIT

template <class T>
struct is_pointer : false_type
{};

template <class T>
struct is_pointer<T*> : true_type
{};

inline u32 wang_hash_32(u32 seed)
{
  seed = (seed ^ 61) ^ (seed >> 16);
  seed *= 9;
  seed = seed ^ (seed >> 4);
  seed *= 0x27d4eb2d;
  seed = seed ^ (seed >> 15);
  return seed;
}

inline u32 xorshift_32(u32 seed)
{
  u32 hash = seed;
  hash ^= hash << 13;
  hash ^= hash >> 17;
  hash ^= hash << 5;
  return hash;
}

inline u64 xorshift_64(u64 seed)
{
  u64 hash = seed;
  hash ^= hash << 13;
  hash ^= hash >> 7;
  hash ^= hash << 17;
  return hash;
}

inline u32 fnv_hash_32(const char* const stream, u64 count)
{
  static const u32 fnv_offset_basis = 0x811c9dc5;
  static const u32 fnv_prime = 0x01000193;

  u32 hash = fnv_offset_basis;
  for (u64 i = 0; i < count; i++)
  {
    hash *= fnv_prime;
    hash ^= stream[count];
  }

  return hash;
}

inline u64 fnv_hash_64(const char* const stream, u64 count)
{
  static const u64 fnv_offset_basis = 0xcbf29ce484222325;
  static const u64 fnv_prime = 0x00000100000001B3;

  u64 hash = fnv_offset_basis;
  for (u64 i = 0; i < count; i++)
  {
    hash *= fnv_prime;
    hash ^= stream[count];
  }

  return hash;
}

template <class T, class = typename enable_if<is_pointer<T>::value || is_integral<T>::value>::type>
inline u32 fnv_hash_32(T const& val)
{
  return fnv_hash_32(reinterpret_cast<const char*>(&val), sizeof(val));
}

template <class T, class = typename enable_if<is_pointer<T>::value || is_integral<T>::value>::type>
inline u64 fnv_hash_64(T const& val)
{
  return fnv_hash_64(reinterpret_cast<const char*>(&val), sizeof(val));
}

template <class T, class = typename enable_if<is_pointer<T>::value || is_integral<T>::value>::type>
class default_hasher
{
public:
  inline u64 operator()(const T& value) const
  {
    return fnv_hash_64(value);
  }
};

template <class T>
class default_hasher<T, typename enable_if<(is_pointer<T>::value || is_integral<T>::value) && sizeof(T) == 4>::type>
{
public:
  inline u64 operator()(T const& val) const
  {
    return xorshift_32(*reinterpret_cast<const u32*>(&val));
  }
};

template <class T>
class default_hasher<T, typename enable_if<(is_pointer<T>::value || is_integral<T>::value) && sizeof(T) == 8>::type>
{
public:
  inline u64 operator()(T const& val) const
  {
    return xorshift_64(*reinterpret_cast<const u64*>(&val));
  }
};

} // namespace util
