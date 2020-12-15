#include <iostream>

#include "segment-allocator.hpp"

std::uint8_t log2(std::uint8_t x) {
  std::uint8_t count = 0;
  while (x > 1) {
    count += 1;
    x /= 2;
  }
  return count;
}

SegmentAllocator::SegmentAllocator(std::size_t size, std::size_t align)
    : align(align), shift_offset(log2(align)) {
  // std::cout << "Align: " << align
  //          << ", amount to shift accesses by: " << shift_offset << '\n';
  all_segments = std::make_unique<SharedSegment[]>(MAX_SEGMENTS);
  available.reserve(MAX_SEGMENTS);
  for (auto i = 1; i <= MAX_SEGMENTS; ++i) {
    available.push_back(MAX_SEGMENTS - i);
  }
  ObjectId dummy;
  allocate(size, &dummy);
}

bool SegmentAllocator::allocate(std::size_t size, ObjectId* addr) {
  // std::cout << "Acquiring mutex\n";
  std::unique_lock lock(mutex);
  // std::cout << "#Available segments: " << available.size() << '\n';
  if (available.empty()) {
    return false;
  }
  auto next = available.back();
  available.pop_back();
  all_segments[next].allocate(size, align);

  *addr = ObjectId{next, 1, 0};
  return true;
}

SharedSegment& SegmentAllocator::find_segment(ObjectId addr) {
  return all_segments[addr.segment];
}

Object& SegmentAllocator::find(ObjectId addr) {
  const auto segm_offset = addr.offset >> shift_offset;
  return find_segment(addr)[segm_offset];
}

void SegmentAllocator::free(ObjectId addr) {
  std::unique_lock lock(mutex);
  all_segments[addr.segment].deallocate();
  available.push_back(addr.segment);
}
