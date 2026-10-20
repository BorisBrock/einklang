#pragma once

#include <array>
#include <atomic>
#include <cstddef>
#include <type_traits>

/// Lock-free single-producer / single-consumer ring buffer.
///
/// Safe to use across exactly two threads: one producer and one consumer.
/// Capacity must be a power of two.  Items are stored by value; prefer small
/// or move-cheap types (e.g. std::vector<float> moved in).
///
/// Usage contract
///   - Only one thread calls push() at a time (producer).
///   - Only one thread calls pop() / size_approx() at a time (consumer).
template<typename T, std::size_t Capacity>
class SpscRingBuffer
{
  static_assert(Capacity > 0 && (Capacity & (Capacity - 1)) == 0,
                "SpscRingBuffer: Capacity must be a power of two");

public:
  /// Copy-push one item.  Returns false (and discards the item) if full.
  bool push(const T& item)
    noexcept(std::is_nothrow_copy_assignable_v<T>)
  {
    const std::size_t w = mWrite.load(std::memory_order_relaxed);
    const std::size_t r = mRead.load(std::memory_order_acquire);
    if (w - r >= Capacity)
      return false; // full
    mData[w & kMask] = item;
    mWrite.store(w + 1, std::memory_order_release);
    return true;
  }

  /// Move-push one item.  Returns false (and discards the item) if full.
  bool push(T&& item)
    noexcept(std::is_nothrow_move_assignable_v<T>)
  {
    const std::size_t w = mWrite.load(std::memory_order_relaxed);
    const std::size_t r = mRead.load(std::memory_order_acquire);
    if (w - r >= Capacity)
      return false; // full
    mData[w & kMask] = std::move(item);
    mWrite.store(w + 1, std::memory_order_release);
    return true;
  }

  /// Pop one item into `out`.  Returns false if empty.
  bool pop(T& out)
    noexcept(std::is_nothrow_move_assignable_v<T>)
  {
    const std::size_t r = mRead.load(std::memory_order_relaxed);
    const std::size_t w = mWrite.load(std::memory_order_acquire);
    if (r == w)
      return false; // empty
    out = std::move(mData[r & kMask]);
    mRead.store(r + 1, std::memory_order_release);
    return true;
  }

  /// Approximate number of items in the ring.  May be stale.
  std::size_t size_approx() const noexcept
  {
    return mWrite.load(std::memory_order_relaxed)
         - mRead.load(std::memory_order_relaxed);
  }

  bool empty() const noexcept { return size_approx() == 0; }

private:
  static constexpr std::size_t kMask = Capacity - 1;

  // Place each counter on its own cache line to prevent false sharing
  // between the producer (writes mWrite) and consumer (writes mRead).
  alignas(64) std::atomic<std::size_t> mWrite{0};
  alignas(64) std::atomic<std::size_t> mRead{0};

  std::array<T, Capacity> mData{};
};
