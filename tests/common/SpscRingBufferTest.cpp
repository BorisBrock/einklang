#include <doctest.h>

#include <memory>
#include <thread>
#include <vector>

#include "../../src/common/SpscRingBuffer.h"

TEST_SUITE("SpscRingBuffer")
{
TEST_CASE("starts empty")
{
  SpscRingBuffer<int, 4> ring;
  int out = 0;
  CHECK(ring.empty());
  CHECK(ring.size_approx() == 0);
  CHECK_FALSE(ring.pop(out));
}

TEST_CASE("pops in FIFO order")
{
  SpscRingBuffer<int, 8> ring;
  for (int i = 0; i < 5; ++i)
    REQUIRE(ring.push(i));
  CHECK(ring.size_approx() == 5);

  for (int i = 0; i < 5; ++i)
  {
    int out = -1;
    REQUIRE(ring.pop(out));
    CHECK(out == i);
  }
  CHECK(ring.empty());
}

TEST_CASE("rejects pushes when full and accepts again after a pop")
{
  SpscRingBuffer<int, 4> ring;
  for (int i = 0; i < 4; ++i)
    REQUIRE(ring.push(i));
  CHECK_FALSE(ring.push(99));
  CHECK(ring.size_approx() == 4);

  int out = -1;
  REQUIRE(ring.pop(out));
  CHECK(out == 0);
  CHECK(ring.push(4));

  for (int expected = 1; expected <= 4; ++expected)
  {
    REQUIRE(ring.pop(out));
    CHECK(out == expected);
  }
}

TEST_CASE("wraps around many times")
{
  SpscRingBuffer<int, 4> ring;
  for (int i = 0; i < 1000; ++i)
  {
    REQUIRE(ring.push(i));
    int out = -1;
    REQUIRE(ring.pop(out));
    CHECK(out == i);
  }
}

TEST_CASE("moves move-only items")
{
  SpscRingBuffer<std::unique_ptr<int>, 2> ring;
  REQUIRE(ring.push(std::make_unique<int>(5)));
  std::unique_ptr<int> out;
  REQUIRE(ring.pop(out));
  REQUIRE(out != nullptr);
  CHECK(*out == 5);
}

TEST_CASE("transfers every item exactly once between two threads")
{
  constexpr int kCount = 200000;
  SpscRingBuffer<int, 64> ring;

  std::thread producer([&]()
  {
    for (int i = 0; i < kCount; ++i)
      while (!ring.push(i))
        std::this_thread::yield();
  });

  bool inOrder = true;
  int next = 0;
  while (next < kCount)
  {
    int out = -1;
    if (ring.pop(out))
    {
      inOrder = inOrder && out == next;
      ++next;
    }
    else
    {
      std::this_thread::yield();
    }
  }
  producer.join();

  CHECK(inOrder);
  CHECK(ring.empty());
}
}
