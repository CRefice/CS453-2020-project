#pragma once

#include <cstddef>
#include <cstring>
#include <memory>

#include "versioned-lock.hpp"

struct ObjectVersion {
  ObjectVersion(std::size_t size) : buf(std::make_unique<char[]>(size)) {
    std::memset(buf.get(), 0, size);
  }

  ObjectVersion(std::unique_ptr<char[]> buf) : buf(std::move(buf)) {}

  std::unique_ptr<char[]> buf;
  VersionedLock::Timestamp version = 0;
  ObjectVersion* earlier = nullptr;

  void read(char* dst, std::size_t size) const noexcept {
    std::memcpy(dst, buf.get(), size);
  }

  void write(const char* src, std::size_t size) const noexcept {
    std::memcpy(buf.get(), src, size);
  }
};

struct Object {
  VersionedLock lock;
  std::atomic<ObjectVersion*> latest{nullptr};
};

class SharedSegment {
public:
  // Compare the shared segment to nullptr to check if the allocation
  // succeeded after construction (this class does not throw exceptions)
  SharedSegment(std::size_t size, std::size_t align)
      : sz(size), objects(new Object[sz]) {
    for (auto i = 0ul; i < (size / align); ++i) {
      objects[i].latest.store(new ObjectVersion(align),
                              std::memory_order_relaxed);
    }
  }

  [[nodiscard]] Object& operator[](std::size_t idx) noexcept {
    return objects[idx];
  }

  [[nodiscard]] const Object& operator[](std::size_t idx) const noexcept {
    return objects[idx];
  }

  [[nodiscard]] std::size_t size() const noexcept { return sz; }

private:
  std::size_t sz;
  std::unique_ptr<Object[]> objects;
};
