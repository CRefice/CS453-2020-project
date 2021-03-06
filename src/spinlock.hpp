#pragma once

#include <atomic>

// Taken from
// https://timur.audio/using-locks-in-real-time-audio-processing-safely
class SpinLock {
public:
  bool try_lock() noexcept {
    return !flag.test_and_set(std::memory_order_acquire);
  }

  void lock() noexcept {
    while (!try_lock()) {
      // spin;
    }
  };

  void unlock() noexcept { flag.clear(std::memory_order_release); }

private:
  std::atomic_flag flag = ATOMIC_FLAG_INIT;
};
