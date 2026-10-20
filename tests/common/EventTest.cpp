#include <doctest.h>

#include <string>
#include <vector>

#include "../../src/common/Event.h"

TEST_SUITE("Event")
{
TEST_CASE("emitting without subscribers does nothing")
{
  Event<int> event;
  event.emit(42);
}

TEST_CASE("every subscriber receives the arguments in subscription order")
{
  Event<int, std::string> event;
  std::vector<std::string> calls;
  event.subscribe([&](int n, std::string s) { calls.push_back("a" + std::to_string(n) + s); });
  event.subscribe([&](int n, std::string s) { calls.push_back("b" + std::to_string(n) + s); });

  event.emit(7, "x");

  CHECK(calls == std::vector<std::string>{"a7x", "b7x"});
}

TEST_CASE("unsubscribe removes only the given handler")
{
  Event<> event;
  int first = 0, second = 0;
  const auto id1 = event.subscribe([&]() { ++first; });
  event.subscribe([&]() { ++second; });

  event.emit();
  event.unsubscribe(id1);
  event.emit();

  CHECK(first == 1);
  CHECK(second == 2);
}

TEST_CASE("handler ids are unique")
{
  Event<> event;
  const auto a = event.subscribe([]() {});
  const auto b = event.subscribe([]() {});
  event.unsubscribe(a);
  const auto c = event.subscribe([]() {});
  CHECK(a != b);
  CHECK(c != a);
  CHECK(c != b);
}

TEST_CASE("unsubscribing an unknown id is harmless")
{
  Event<> event;
  int calls = 0;
  event.subscribe([&]() { ++calls; });
  event.unsubscribe(12345);
  event.emit();
  CHECK(calls == 1);
}
}
