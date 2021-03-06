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

SharedMemory::~SharedMemory() noexcept { unref(current.load()); }

Transaction SharedMemory::begin_tx(bool is_ro) noexcept {
  Transaction tx;
  tx.is_ro = is_ro;
  TransactionDescriptor* start_point;
  {
    std::unique_lock lock(descriptor_mutex);
    start_point = current.load(std::memory_order_acquire);
    ref(start_point);
  }

  tx.start_time = start_point->commit_time;
  tx.start_point = start_point;
  return tx;
}

bool SharedMemory::read_word(Transaction& tx, ObjectId src,
                             char* dst) noexcept {
  // std::cout << "Reading word " << src.offset << ' ' << +src.segment << '\n';
  auto& obj = allocator.find(src);
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
    abort(tx);
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
                              ObjectId dst) noexcept {
  // std::cout << "Writing word " << dst.offset << ' ' << +dst.segment << '\n';
  if (auto entry = tx.find_write_entry(dst)) {
    std::memcpy(entry->written.get(), src, align);
    return true;
  }

  auto& obj = allocator.find(dst);
  auto written = clone(src, align);
  tx.write_set.push_back({dst, obj, std::move(written)});
  return true;
}

bool SharedMemory::allocate(Transaction& tx, std::size_t size,
                            ObjectId* dest) noexcept {
  auto success = allocator.allocate(size, dest);
  if (success) {
    tx.alloc_set.push_back(*dest);
  }
  return success;
}

void SharedMemory::free(Transaction& tx, ObjectId addr) noexcept {
  if (allocator.find_segment(addr).mark_for_deletion()) {
    tx.free_set.push_back(addr);
  }
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
  auto rollback_locks = [&it, &tx] {
    unlock_all(tx.write_set.begin(), it);
    // std::cout << "Rolling back transaction with alloc set of size "
    //           << tx.alloc_set.size() << '\n';
  };

  std::unordered_set<std::size_t> acquired_locks;
  // std::cout << "Acquiring write set:\n";
  while (it != tx.write_set.end()) {
    // std::cout << it->addr.offset << '\n';
    if (!it->obj.lock.try_lock(tx.start_time)) {
      rollback_locks();
      abort(tx);
      return false;
    }
    acquired_locks.insert(opaque(it->addr));
    it++;
  }

  // std::cout << "Validating read set: \n";
  // Validate read set
  for (auto& read : tx.read_set) {
    if (acquired_locks.find(opaque(read.addr)) != acquired_locks.end()) {
      continue;
    }
    if (!read.obj.lock.validate(tx.start_time)) {
      /*
      std::cout << "lock validation of object " << read.addr.offset
                << " failed: start_time=" << tx.start_time
                << " but lock_version=" << read.obj.lock.version()
                << " and locked=" << read.obj.lock.locked() << '\n';
      */
      rollback_locks();
      abort(tx);
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

void SharedMemory::abort(Transaction& tx) {
  for (auto segment : tx.alloc_set) {
    allocator.free(segment);
  }
  for (auto segment : tx.free_set) {
    allocator.find_segment(segment).cancel_deletion();
  }
  unref(tx.start_point);
}

void SharedMemory::commit_changes(Transaction& tx) {
  auto cur_point = current.load(std::memory_order_acquire);

  // std::cout << "On commit, cur_point has refcount="
  //          << cur_point->refcount.load() << '\n';

  auto commit_time = cur_point->commit_time + 1;
  auto* descr = new TransactionDescriptor{commit_time};

  cur_point->next = descr;
  ref(descr);

  // Since current is updated to descr
  unref(cur_point);

  // refcount is already 1 since current now holds that
  current.store(descr, std::memory_order_release);

  descr->segments_to_delete = std::move(tx.free_set);

  for (auto& write : tx.write_set) {
    auto& obj = write.obj;

    auto* old_version = obj.latest.load(std::memory_order_acquire);

    auto* new_version = new ObjectVersion(std::move(write.written));

    new_version->version = commit_time;
    new_version->earlier = old_version;

    obj.latest.store(new_version, std::memory_order_release);
    descr->objects_to_delete.emplace_back(old_version);

    // std::cout << "unlocking object " << write.addr.offset
    //          << " with timestamp=" << commit_time << '\n';
    obj.lock.unlock(commit_time);
  }

  unref(tx.start_point);
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
    // std::cout << "Actually freeing segment " << +segm.segment << '\n';
    allocator.free(segm);
  }
}
