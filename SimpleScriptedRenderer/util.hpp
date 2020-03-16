#pragma once

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
constexpr void swap(T& lhs, T& rhs)
{
  T tmp = move(lhs);
  lhs = move(rhs);
  rhs = move(tmp);
}
