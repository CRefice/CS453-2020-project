#pragma once

#include <shared_mutex>
#include <vector>

#include "shared-segment.hpp"

class SegmentAllocator {
public:
  SegmentAllocator(std::size_t obj_size) : obj_size(obj_size) {}

  std::size_t allocate(std::size_t size);
  void free(std::size_t addr);

  Object* find(std::size_t addr);

  const SharedSegment& first_segment() const noexcept {
    return allocated[0].segm;
  }

  std::size_t first_addr() const noexcept { return 1; }

private:
  struct AllocatedSegment {
    std::size_t id;
    SharedSegment segm;
  };

  AllocatedSegment* find_segment(std::size_t addr);

  std::shared_mutex mutex;
  std::vector<AllocatedSegment> allocated;
  std::size_t next_id = 1;
  std::size_t obj_size;
};
