#pragma once

#include <algorithm>
#include <cstdint>
#include <functional>
#include <map>

// std::less forms a total order on pointers.
// That's why we use a map.
template <typename Key>
class RegionTree {
public:
  using iterator = typename std::map<Key, std::size_t>::iterator;

  void insert(Key&& ptr, std::size_t size) { ranges.emplace(ptr, size); }
  void remove(const Key& ptr) { ranges.erase(ptr); }
  void merge(RegionTree&& other) { ranges.merge(std::move(other.ranges)); }

  iterator find(void* ptr) {
  }

private:
  std::map<Key, std::size_t> ranges;
};
