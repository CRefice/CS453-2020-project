#include <algorithm>
#include <cassert>
#include <iostream>
#include <unordered_set>

#include "shared-memory.hpp"

static std::unique_ptr<char[]> clone(const char* word, std::size_t align) {
  auto copy = std::make_unique<char[]>(align);
  std::memcpy(copy.get(), word, align);
  return copy;
}

Transaction SharedMemory::begin_tx(bool is_ro) noexcept {
  Transaction tx;
  tx.is_ro = is_ro;
  TransactionDescriptor* start_point;
  {
    std::unique_lock lock(descriptor_mutex);
    start_point = current.load(std::memory_order_acquire);
    if (is_ro) {
      ref(start_point);
    }
  }

  tx.start_time = start_point->commit_time;
  if (is_ro) {
    tx.start_point = start_point;
  }
  return tx;
}

bool SharedMemory::read_word(Transaction& tx, std::size_t src,
                             char* dst) noexcept {
  auto ptr = allocator.find(src);
  auto& obj = *ptr;
  if (tx.is_ro) {
    read_word_readonly(tx, obj, dst);
    return true;
  }

  if (auto entry = tx.find_write_entry(src)) {
    std::memcpy(dst, entry->written.get(), align);
    return true;
  }

  auto latest = obj.latest.load(std::memory_order_acquire);
  if (!obj.lock.validate(tx.start_time)) {
    return false;
  }
  tx.read_set.push_back({src, obj});
  latest->read(dst, align);
  return true;
}

void SharedMemory::read_word_readonly(const Transaction& tx, const Object& obj,
                                      char* dst) const noexcept {
  auto ver = obj.latest.load(std::memory_order_acquire);
  while (ver->version > tx.start_time) {
    ver = ver->earlier;
  }
  ver->read(dst, align);
}

bool SharedMemory::write_word(Transaction& tx, const char* src,
                              std::size_t dst) noexcept {
  if (auto entry = tx.find_write_entry(dst)) {
    std::memcpy(entry->written.get(), src, align);
    return true;
  }

  auto ptr = allocator.find(dst);
  auto& obj = *ptr;
  auto written = clone(src, align);
  tx.write_set.push_back({dst, obj, std::move(written)});
  return true;
}

static void unlock_all(std::vector<Transaction::WriteEntry>::iterator begin,
                       std::vector<Transaction::WriteEntry>::iterator end) {
  while (begin != end) {
    // Unlock without changing the version
    begin->obj.lock.unlock();
    begin++;
  }
}

bool SharedMemory::end_tx(Transaction& tx) noexcept {
  if (tx.is_ro) {
    unref(tx.start_point);
    return true;
  }

  // First, try acquiring all locks in the write set
  auto it = tx.write_set.begin();
  auto rollback = [this, &it, &tx] {
    unlock_all(tx.write_set.begin(), it);
    for (auto alloc : tx.alloc_set) {
      allocator.free(alloc);
    }
  };

  std::unordered_set<std::size_t> acquired_locks;
  // std::cout << "Acquiring write set:\n";
  while (it != tx.write_set.end()) {
    // std::cout << it->addr << '\n';
    if (!it->obj.lock.try_lock(tx.start_time)) {
      rollback();
      return false;
    }
    acquired_locks.insert(it->addr);
    it++;
  }

  // std::cout << "Validating read set: \n";
  // Validate read set
  for (auto& read : tx.read_set) {
    if (acquired_locks.find(read.addr) != acquired_locks.end()) {
      continue;
    }
    if (!read.obj.lock.validate(tx.start_time)) {
      // std::cout << "lock validation of object " << read.addr
      //          << " failed: start_time=" << tx.start_time
      //          << " but lock_version=" << read.obj.lock.version()
      //          << " and locked=" << read.obj.lock.locked() << '\n';
      rollback();
      return false;
    }
  }

  // std::cout << "Committing changes\n";
  {
    std::unique_lock lock(descriptor_mutex);
    commit_changes(tx);
  }

  return true;
}

void SharedMemory::commit_changes(Transaction& tx) {
  auto cur_point = current.load(std::memory_order_acquire);

  auto commit_time = cur_point->commit_time + 1;
  auto* descr = new TransactionDescriptor;
  descr->refcount.store(2, std::memory_order_release);
  descr->commit_time = commit_time;
  for (auto& write : tx.write_set) {
    auto& obj = write.obj;

    auto* old_version = obj.latest.load(std::memory_order_acquire);

    auto* new_version = new ObjectVersion(std::move(write.written));

    new_version->version = commit_time;
    new_version->earlier = old_version;

    obj.latest.store(new_version, std::memory_order_release);
    descr->objects_to_delete.emplace_back(old_version);
    descr->segments_to_delete = std::move(tx.free_set);

    // std::cout << "unlocking object " << write.addr
    //          << " with timestamp=" << commit_time << '\n';
    obj.lock.unlock(commit_time);
  }
  current.store(descr, std::memory_order_release);

  cur_point->next = descr;
  unref(cur_point);
}

std::size_t SharedMemory::allocate(Transaction& tx, std::size_t dest) noexcept {
  auto id = allocator.allocate(dest);
  tx.alloc_set.push_back(id);
  return id;
}

void SharedMemory::free(Transaction& tx, std::size_t id) noexcept {
  tx.free_set.push_back(id);
}

void SharedMemory::ref(TransactionDescriptor* desc) {
  if (desc == nullptr) {
    return;
  }
  desc->refcount.fetch_add(1, std::memory_order_acq_rel);
}

void SharedMemory::unref(TransactionDescriptor* desc) {
  if (desc == nullptr) {
    return;
  }

  auto previous = desc->refcount.fetch_sub(1, std::memory_order_acq_rel);
  if (previous == 1) {
    unref(desc->next);
    commit_frees(*desc);
    delete desc;
  }
}

void SharedMemory::commit_frees(TransactionDescriptor& desc) {
  for (auto segm : desc.segments_to_delete) {
    allocator.free(segm);
  }
}
