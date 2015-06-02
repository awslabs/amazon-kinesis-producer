// Copyright 2015 Amazon.com, Inc. or its affiliates. All Rights Reserved.
//
// Licensed under the Amazon Software License (the "License").
// You may not use this file except in compliance with the License.
// A copy of the License is located at
//
//  http://aws.amazon.com/asl
//
// or in the "license" file accompanying this file. This file is distributed
// on an "AS IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
// express or implied. See the License for the specific language governing
// permissions and limitations under the License.

#ifndef AWS_UTILS_CONCURRENT_LINKED_QUEUE_H_
#define AWS_UTILS_CONCURRENT_LINKED_QUEUE_H_

#include <atomic>

#include <boost/lockfree/stack.hpp>

namespace aws {
namespace utils {

namespace concurrent_linked_queue {

template <typename T>
class Node;

template <typename T>
class NodePtr {
 public:
  inline NodePtr(uint64_t counter = 0, Node<T>* node = nullptr)
      : counter_(counter),
        node_(node) {}

  inline Node<T>* node() {
    return node_;
  }

  inline Node<T>* operator ->() {
    return node_;
  }

  inline NodePtr<T> copy_incr_cnt(Node<T>* new_node = nullptr) {
    return copy(new_node, 1);
  }

  inline NodePtr<T> copy_decr_cnt(Node<T>* new_node = nullptr) {
    return copy(new_node, 0, 1);
  }

  inline NodePtr<T> copy(Node<T>* new_node = nullptr,
                         uint64_t incr_cnt = 0,
                         uint64_t decr_cnt = 0) {
    return NodePtr(counter_ + incr_cnt - decr_cnt,
                   new_node == nullptr ? node_ : new_node);
  }

  inline bool operator ==(const NodePtr<T>& other) const {
    return std::memcmp(this, &other, sizeof(NodePtr<T>)) == 0;
  }

  inline bool operator !=(const NodePtr<T>& other) const {
    return !(*this == other);
  }

  inline uint64_t counter() const {
    return counter_;
  }

 private:
  uint64_t counter_;
  Node<T>* node_;
};

template <typename T>
class Node {
 public:
  inline Node(T&& data, bool is_dummy = false)
      : data_(std::move(data)),
        is_dummy_(is_dummy),
        next_(kSpecialTag),
        prev_(kSpecialTag) {}

  inline Node()
      : Node(T(), true) {}

  inline void reset(T&& data, bool is_dummy = false) {
    data_ = std::move(data);
    is_dummy_ = is_dummy;
    next_.store(kSpecialTag);
    prev_.store(kSpecialTag);
  }

  inline void reset() {
    reset(T(), true);
  }

  inline std::atomic<NodePtr<T>>& next() {
    return next_;
  }

  inline std::atomic<NodePtr<T>>& prev() {
    return prev_;
  }

  inline bool is_dummy() const {
    return is_dummy_;
  }

  inline T& data() {
    return data_;
  }

 private:
  static constexpr const uint64_t kSpecialTag =
      std::numeric_limits<uint64_t>::max() / 2;

  T data_;
  bool is_dummy_;
  std::atomic<NodePtr<T>> next_;
  std::atomic<NodePtr<T>> prev_;
};

} //namespace concurrent_linked_queue

// Based on Ladan-Mozes/Shavit:
// https://www.offblast.org/stuff/books/FIFO_Queues.pdf
//
// The queue is lockfree only if the standard memory allocator is lockfree.
//
// The enqueue is move-only. The dequeue also moves.
template <typename T>
class ConcurrentLinkedQueue {
 public:
  ConcurrentLinkedQueue()
      : head_(NodePtr()),
        tail_(NodePtr()) {
    auto dummy = new Node();
    head_.store(NodePtr(0, dummy));
    tail_.store(NodePtr(0, dummy));
  }

  ~ConcurrentLinkedQueue() {
    T t;
    while (try_take(t));

    auto last_dummy = head_.load().node();
    if (last_dummy != nullptr) {
      delete last_dummy;
    }
  }

  void put(T&& data) {
    auto new_node = new Node(std::move(data));
    while (true) {
      NodePtr old_tail(tail_);
      new_node->next().store(old_tail.copy_incr_cnt());
      NodePtr new_tail = old_tail.copy_incr_cnt(new_node);
      if (tail_.compare_exchange_strong(old_tail, new_tail)) {
        old_tail->prev().store(old_tail.copy(new_node));
        break;
      }
    }
  }

  bool try_take(T& t) {
    while (true) {
      NodePtr old_head(head_);
      NodePtr old_tail(tail_);
      NodePtr old_prev(old_head->prev());
      if (old_head == head_.load()) {
        if (!old_head->is_dummy()) {
          if (old_head != old_tail) {
            if (old_prev.counter() != old_head.counter()) {
              fix_list(old_tail, old_head);
              continue;
            }
          } else {
            Node* dummy = new Node();
            dummy->next().store(old_tail.copy_incr_cnt());
            NodePtr new_tail = old_tail.copy_incr_cnt(dummy);
            if (tail_.compare_exchange_strong(old_tail, new_tail)) {
              old_head->prev().store(old_tail.copy(dummy));
            } else {
              delete dummy;
            }
            continue;
          }
          NodePtr new_head = old_head.copy_incr_cnt(old_prev.node());
          if (head_.compare_exchange_strong(old_head, new_head)) {
            t = std::move(old_head->data());
            delete old_head.node();
            return true;
          }
        } else {
          if (old_head == old_tail) {
            return false;
          } else {
            if (old_prev.counter() != old_head.counter()) {
              fix_list(old_tail, old_head);
              continue;
            }
            NodePtr new_head = old_head.copy_incr_cnt(old_prev.node());
            // The paper's original code seems to leak memory at this part, so
            // we've added a delete.
            if (head_.compare_exchange_strong(old_head, new_head)) {
              delete old_head.node();
            }
          }
        }
      }
    }
  }

 private:
  using Node = concurrent_linked_queue::Node<T>;
  using NodePtr = concurrent_linked_queue::NodePtr<T>;

  inline void fix_list(NodePtr& old_tail, NodePtr& old_head) {
    NodePtr cur(old_tail);
    while (old_head == head_ && cur != old_head) {
      NodePtr cur_next(cur->next());
      if (cur_next.counter() != cur.counter()) {
        return;
      }
      NodePtr next_prev(cur_next->prev());
      NodePtr fix = cur.copy_decr_cnt();
      if (next_prev != fix) {
        cur_next->prev().store(fix);
      }
      cur = cur_next.copy_decr_cnt();
    }
  }

  std::atomic<NodePtr> head_;
  std::atomic<NodePtr> tail_;
};

} //namespace utils
} //namespace aws

#endif //AWS_UTILS_CONCURRENT_LINKED_QUEUE_H_
