#pragma once

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
constexpr auto move(T&& v)
{
  return static_cast<typename remove_reference<T>::type&&>(v);
}

template <class T>
constexpr T&& forward(typename remove_reference<T>::type& v)
{
  return static_cast<T&&>(v);
}

template <class T>
constexpr T&& forward(typename remove_reference<T>::type&& v)
{
  return static_cast<T&&>(v);
}

template <class T>
constexpr void swap(T& lhs, T& rhs)
{
  T tmp = move(lhs);
  lhs = move(rhs);
  rhs = move(tmp);
}
} // namespace util
