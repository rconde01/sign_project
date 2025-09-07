#pragma once

class FrtosMutex {
public:
  FrtosMutex(){
    m_mux = portMUX_INITIALIZER_UNLOCKED;
  }

  void lock(){
    portENTER_CRITICAL(&m_mux);
  }

  void unlock(){
    portEXIT_CRITICAL(&m_mux);
  }
private:
  portMUX_TYPE m_mux;
};

class FrtosMutexLock {
public:
  FrtosMutexLock(FrtosMutex & mutex):m_mutex{mutex}{
    m_mutex.lock();
  }

  ~FrtosMutexLock(){
    m_mutex.unlock();
  }

private:
  FrtosMutex & m_mutex;
};

