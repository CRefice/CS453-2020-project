/**
 * @file   tm.cpp
 * @author Carlo Refice
 *
 * @section LICENSE
 *
 * [...]
 *
 * @section DESCRIPTION
 *
 * Implementation of your own transaction manager.
 * You can completely rewrite this file (and create more files) as you wish.
 * Only the interface (i.e. exported symbols and semantic) must be preserved.
 **/

// Requested features
#ifndef _GNU_SOURCE
#define _GNU_SOURCE
#endif
#define _POSIX_C_SOURCE 200809L
#ifdef __STDC_NO_ATOMICS__
#error Current C11 compiler does not support atomic operations
#endif

// External headers
#include <iostream>

// Internal headers
#include "shared-memory.hpp"
#include "tm.hpp"

// -------------------------------------------------------------------------- //
/** Define a proposition as likely true.
 * @param prop Proposition
 **/
#undef likely
#ifdef __GNUC__
#define likely(prop) __builtin_expect((prop) ? 1 : 0, 1)
#else
#define likely(prop) (prop)
#endif

/** Define a proposition as likely false.
 * @param prop Proposition
 **/
#undef unlikely
#ifdef __GNUC__
#define unlikely(prop) __builtin_expect((prop) ? 1 : 0, 0)
#else
#define unlikely(prop) (prop)
#endif

/** Define one or several attributes.
 * @param type... Attribute names
 **/
#undef as
#ifdef __GNUC__
#define as(type...) __attribute__((type))
#else
#define as(type...)
#warning This compiler has no support for GCC attributes
#endif

shared_t opaque(SharedMemory* mem) { return reinterpret_cast<shared_t>(mem); }
tx_t opaque(Transaction* tx) { return reinterpret_cast<tx_t>(tx); }

SharedMemory* transparent(shared_t shared) {
  return reinterpret_cast<SharedMemory*>(shared);
}
Transaction* transparent(tx_t tx) { return reinterpret_cast<Transaction*>(tx); }

// -------------------------------------------------------------------------- //
/** Create (i.e. allocate + init) a new shared memory region, with one
 * first non-free-able allocated segment of the requested size and
 * alignment.
 * @param size  Size of the first shared segment of memory to allocate
 *  (in bytes), must be a positive multiple of the alignment
 * @param align Alignment (in bytes, must be a power of 2) that the
 *  shared memory region must support
 * @return Opaque shared memory region handle, 'invalid_shared' on failure
 **/
shared_t tm_create(size_t size, size_t align) noexcept {
  return opaque(new SharedMemory(size, align));
}

/** Destroy (i.e. clean-up + free) a given shared memory region.
 * @param shared Shared memory region to destroy, with no running transaction
 **/
void tm_destroy(shared_t shared) noexcept { delete transparent(shared); }

/** [thread-safe] Return the start address of the first allocated segment in the
 *shared memory region.
 * @param shared Shared memory region to query
 * @return Start address of the first allocated segment
 **/
void* tm_start(shared_t shared) noexcept {
  auto addr = transparent(shared)->start_addr();
  return reinterpret_cast<void*>(opaque(addr));
}

/** [thread-safe] Return the size (in bytes) of the first allocated segment of
 *the shared memory region.
 * @param shared Shared memory region to query
 * @return First allocated segment size
 **/
size_t tm_size(shared_t shared) noexcept {
  auto ret = transparent(shared)->size();
  return ret;
}

/** [thread-safe] Return the alignment (in bytes) of the memory accesses on the
 *given shared memory region.
 * @param shared Shared memory region to query
 * @return Alignment used globally
 **/
size_t tm_align(shared_t shared) noexcept {
  return transparent(shared)->alignment();
}

/** [thread-safe] Begin a new transaction on the given shared memory region.
 * @param shared Shared memory region to start a transaction on
 * @param is_ro  Whether the transaction is read-only
 * @return Opaque transaction ID, 'invalid_tx' on failure
 **/
tx_t tm_begin(shared_t shared, bool is_ro) noexcept {
  // std::cout << "Starting new " << (is_ro ? "readonly" : "writable") << "
  // tx\n";
  return opaque(new Transaction(transparent(shared)->begin_tx(is_ro)));
}

/** [thread-safe] End the given transaction.
 * @param shared Shared memory region associated with the transaction
 * @param tx     Transaction to end
 * @return Whether the whole transaction committed
 **/
bool tm_end(shared_t shared, tx_t tx) noexcept {
  // std::cout << "Committing tx ";
  bool success = transparent(shared)->end_tx(*transparent(tx));
  // std::cout << (success ? "succeeded" : "failed") << '\n';
  delete transparent(tx);
  return success;
}

/** [thread-safe] Read operation in the given transaction, source in the shared
 *region and target in a private region.
 * @param shared Shared memory region associated with the transaction
 * @param tx     Transaction to use
 * @param source Source start address (in the shared region)
 * @param size   Length to copy (in bytes), must be a positive multiple of the
 *alignment
 * @param target Target start address (in a private region)
 * @return Whether the whole transaction can continue
 **/
bool tm_read(shared_t shared, tx_t tx, void const* source, size_t size,
             void* target) noexcept {
  auto* tm = transparent(shared);
  auto* dest = reinterpret_cast<char*>(target);

  auto start = to_object_id(source);
  std::size_t offset = 0;
  auto align = tm->alignment();
  // std::cout << "Original source ptr=" << source << '\n';
  // std::cout << "Reading :: size=" << size << ", start=" << start.offset
  //          << ", segment=" << +start.segment << ", align=" << align << '\n';
  while (offset < size) {
    if (tm->read_word(*transparent(tx), start + offset, dest + offset) ==
        false) {
      delete transparent(tx);
      return false;
    }
    offset += align;
  }
  return true;
}

/** [thread-safe] Write operation in the given transaction, source in a private
 *region and target in the shared region.
 * @param shared Shared memory region associated with the transaction
 * @param tx     Transaction to use
 * @param source Source start address (in a private region)
 * @param size   Length to copy (in bytes), must be a positive multiple of the
 *alignment
 * @param target Target start address (in the shared region)
 * @return Whether the whole transaction can continue
 **/
bool tm_write(shared_t shared, tx_t tx, void const* source, size_t size,
              void* target) noexcept {
  auto* tm = transparent(shared);
  const auto* src = reinterpret_cast<const char*>(source);

  auto start = to_object_id(target);

  std::size_t offset = 0;
  auto align = tm->alignment();

  // std::cout << "Original target ptr=" << target << '\n';
  // std::cout << "Writing :: size=" << size << ", start=" << start.offset
  //           << ", segment=" << +start.segment << ", align=" << align << '\n';
  while (offset < size) {
    if (tm->write_word(*transparent(tx), src + offset, start + offset) ==
        false) {
      delete transparent(tx);
      return false;
    }
    offset += align;
  }
  return true;
}

/** [thread-safe] Memory allocation in the given transaction.
 * @param shared Shared memory region associated with the transaction
 * @param tx     Transaction to use
 * @param size   Allocation requested size (in bytes), must be a positive
 *multiple of the alignment
 * @param target Pointer in private memory receiving the address of the first
 *byte of the newly allocated, aligned segment
 * @return Whether the whole transaction can continue (success/nomem), or not
 *(abort_alloc)
 **/
Alloc tm_alloc(shared_t shared, tx_t tx, size_t size, void** target) noexcept {
  // std::cout << "Alloc'ing to tx\n";
  ObjectId addr;
  bool success = transparent(shared)->allocate(*transparent(tx), size, &addr);
  // std::cout << "Allocation " << (success ? "succeeded" : "failed") << '\n';
  if (success) {
    *target = reinterpret_cast<void*>(opaque(addr));
  }
  return success ? Alloc::success : Alloc::nomem;
}

/** [thread-safe] Memory freeing in the given transaction.
 * @param shared Shared memory region associated with the transaction
 * @param tx     Transaction to use
 * @param target Address of the first byte of the previously allocated segment
 *to deallocate
 * @return Whether the whole transaction can continue
 **/
bool tm_free(shared_t shared, tx_t tx, void* target) noexcept {
  // std::cout << "Free'ing to tx\n";
  auto id = to_object_id(target);
  transparent(shared)->free(*transparent(tx), id);
  return true;
}
