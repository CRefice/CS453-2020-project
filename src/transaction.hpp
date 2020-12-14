#pragma once

#include <atomic>
#include <vector>

#include "shared-segment.hpp"

/*
struct TransactionDescriptor;

class SharedDescriptorRef {
public:
  SharedDescriptorRef(TransactionDescriptor* tx) noexcept;
  ~SharedDescriptorRef() noexcept;

  SharedDescriptorRef(const SharedDescriptorRef& other);
  SharedDescriptorRef& operator=(const SharedDescriptorRef& other);

  SharedDescriptorRef(SharedDescriptorRef&& other) = default;
  SharedDescriptorRef& operator=(SharedDescriptorRef&& other) = default;

  TransactionDescriptor* operator->() noexcept { return ptr; }
  const TransactionDescriptor* operator->() const noexcept { return ptr; }

private:
  TransactionDescriptor* ptr;
};
*/

struct TransactionDescriptor {
  VersionedLock::Timestamp commit_time = 0;
  std::atomic_uint_fast32_t refcount{1};
  std::vector<std::unique_ptr<ObjectVersion>> objects_to_delete{};
  std::vector<std::size_t> segments_to_delete{};

  TransactionDescriptor* next = nullptr;
};

struct Transaction {
  struct WriteEntry {
    std::size_t addr;
    Object& obj;
    std::unique_ptr<char[]> written;
  };

  struct ReadEntry {
    std::size_t addr;
    Object& obj;
  };

  [[nodiscard]] WriteEntry* find_write_entry(std::size_t addr) noexcept {
    for (auto& entry : write_set) {
      if (entry.addr == addr) {
        return &entry;
      }
    }
    return nullptr;
  }

  bool is_ro;
  TransactionDescriptor* start_point;
  VersionedLock::Timestamp start_time;
  std::vector<WriteEntry> write_set;
  std::vector<ReadEntry> read_set;
  std::vector<std::size_t> alloc_set;
  std::vector<std::size_t> free_set;
};
