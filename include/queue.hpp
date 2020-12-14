#pragma once

#include <atomic>
#include <cstdint>
#include <cstdlib>
#include <cstring>
#include <memory>
#include <new>

struct Node;
using Link = std::atomic<Node*>;

struct Node {
  Link next = nullptr, prev = nullptr;
  void* data = nullptr;
};

class AtomicQueue {
public:
  void append(size_t size, size_t align) {
    auto new_tail = new_link(size, align);
    new_tail->prev = tail.load(std::memory_order_relaxed);

    // now make new_node the new head, but if the head
    // is no longer what's stored in new_node->next
    // (some other thread must have inserted a node just now)
    // then put that new head into new_node->next and try again
    while (!head.compare_exchange_weak(new_node->next, new_node,
                                       std::memory_order_release,
                                       std::memory_order_relaxed))
      ; // the body of the loop is empty
  }

private:
  Link new_link(size_t size, size_t align) {
    size_t align_alloc =
        align < sizeof(void*)
            ? sizeof(void*)
            : align; // Also satisfy alignment requirement of 'struct link'

    Node* node = new (std::nothrow) Node;
    if (likely(node)) {
      char* ptr = std::aligned_alloc(size + sizeof(Node*), align_alloc);
      *std::launder(reinterpret_cast<Node*>(ptr)) = node;
      node->data = ptr + sizeof(Node*);
      std::memset(node->data, 0, size);
    }
    return AtomicQueue{node};
  }

  Link head, tail;
};
