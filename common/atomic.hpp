#pragma once

#include "./mutex.hpp"

template<typename T>
class MyAtomic {
public:
  MyAtomic(T value):m_mutex{},m_value{value}{}

  MyAtomic & operator=(T value){
    FrtosMutexLock lock(m_mutex);
    m_value = value;

    return *this;
  }

  operator T() const {
    FrtosMutexLock lock(m_mutex);
    return m_value;
  }

private:
  mutable FrtosMutex m_mutex;
  T m_value;
};