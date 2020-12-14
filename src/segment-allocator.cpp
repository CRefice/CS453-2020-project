#include <mutex>

#include "segment-allocator.hpp"

std::size_t SegmentAllocator::allocate(std::size_t size) {
  auto id = next_id;
  std::unique_lock lock(mutex);
  next_id += size;
  allocated.push_back({id, SharedSegment(size, obj_size)});
  return id;
}

SegmentAllocator::AllocatedSegment*
SegmentAllocator::find_segment(std::size_t addr) {
  std::shared_lock lock(mutex);
  for (auto& entry : allocated) {
    if (entry.id <= addr && (addr - entry.id) < entry.segm.size()) {
      return &entry;
    }
  }
  return nullptr;
}

Object* SegmentAllocator::find(std::size_t addr) {
  auto segment = find_segment(addr);
  if (segment == nullptr) {
    return nullptr;
  }
  auto offset = (addr - segment->id) / obj_size;
  return &segment->segm[offset];
}

void SegmentAllocator::free(std::size_t addr) {
  std::unique_lock lock(mutex);
  for (auto it = allocated.begin(); it != allocated.end(); ++it) {
    if (it->id == addr) {
      allocated.erase(it);
      return;
    }
  }
}
