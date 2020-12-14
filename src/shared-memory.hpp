#pragma once

#include <cstdint>
#include <mutex>
#include <utility>
#include <vector>

#include "segment-allocator.hpp"
#include "shared-segment.hpp"
#include "transaction.hpp"

class SharedMemory {
public:
  SharedMemory(std::size_t size, std::size_t align) noexcept
      : align(align), allocator(align) {
    allocator.allocate(size);
  }

  [[nodiscard]] Transaction begin_tx(bool is_ro) noexcept;
  bool end_tx(Transaction& tx) noexcept;

  bool read_word(Transaction& tx, std::size_t src, char* dest) noexcept;
  bool write_word(Transaction& tx, const char* src, std::size_t dest) noexcept;

  std::size_t allocate(Transaction& tx, std::size_t size) noexcept;
  void free(Transaction& tx, std::size_t id) noexcept;

  [[nodiscard]] std::size_t size() const noexcept {
    return allocator.first_segment().size();
  };

  [[nodiscard]] std::size_t alignment() const noexcept { return align; };

  [[nodiscard]] std::size_t start_addr() const noexcept {
    return allocator.first_addr();
  }

private:
  void ref(TransactionDescriptor* desc);
  void unref(TransactionDescriptor* desc);

  void commit_frees(TransactionDescriptor& desc);

  void commit_changes(Transaction& tx);

  void read_word_readonly(const Transaction& tx, const Object& obj,
                          char* dest) const noexcept;

  std::size_t align;
  SegmentAllocator allocator;
  std::atomic<TransactionDescriptor*> current{new TransactionDescriptor{}};
  std::mutex descriptor_mutex;
};
