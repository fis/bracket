#include "base/owner_set.h"
#include "gtest/gtest.h"

namespace base {

TEST(OwnerSetTest, Size) {
  owner_set<int> set;

  EXPECT_EQ(set.size(), 0);
  int* p = set.emplace(123);
  EXPECT_EQ(set.size(), 1);
  int* q = set.emplace(456);
  EXPECT_EQ(set.size(), 2);
  set.erase(p);
  EXPECT_EQ(set.size(), 1);
  set.erase(q);
  EXPECT_EQ(set.size(), 0);
}

TEST(OwnerSetTest, Empty) {
  owner_set<int> set;

  EXPECT_TRUE(set.empty());
  int* p = set.emplace(123);
  EXPECT_FALSE(set.empty());
  int* q = set.emplace(456);
  EXPECT_FALSE(set.empty());
  set.erase(p);
  EXPECT_FALSE(set.empty());
  set.erase(q);
  EXPECT_TRUE(set.empty());
}

TEST(OwnerSetTest, OwnershipTransfer) {
  std::unique_ptr<int> first = std::make_unique<int>(123);
  std::unique_ptr<int> last;
  owner_set<int> set;

  EXPECT_TRUE((bool) first);
  EXPECT_FALSE((bool) last);
  EXPECT_EQ(set.size(), 0);

  int* p = first.get();
  int* q = set.insert(std::move(first));

  EXPECT_EQ(p, q);

  EXPECT_FALSE((bool) first);
  EXPECT_FALSE((bool) last);
  EXPECT_EQ(set.size(), 1);

  last = set.claim(p);

  EXPECT_FALSE((bool) first);
  EXPECT_TRUE((bool) last);
  EXPECT_EQ(set.size(), 0);

  EXPECT_EQ(p, last.get());
}

TEST(OwnerSetTest, EraseNotFound) {
  owner_set<int> set;
  set.emplace(123);
  auto q = std::make_unique<int>(456);
  bool erased = set.erase(q.get());
  EXPECT_FALSE(erased);
  EXPECT_EQ(set.size(), 1);
}

struct DestroySpy {
  int value;
  bool* flag;
  DestroySpy(int value, bool* flag) : value(value), flag(flag) {}
  ~DestroySpy() { *flag = true; }
};

TEST(OwnerSetTest, DestroyOnErase) {
  owner_set<DestroySpy> set;

  bool a_destroyed = false;
  bool b_destroyed = false;

  DestroySpy* a = set.emplace(123, &a_destroyed);
  DestroySpy* b = set.emplace(456, &b_destroyed);

  EXPECT_EQ(a->value, 123);
  EXPECT_EQ(b->value, 456);

  bool erased = set.erase(a);

  EXPECT_TRUE(erased);
  EXPECT_TRUE(a_destroyed);
  EXPECT_FALSE(b_destroyed);
  EXPECT_EQ(set.size(), 1);
}

TEST(OwnerSetTest, DestroyOnCleanup) {
  bool destroyed = false;
  {
    owner_set<DestroySpy> set;
    set.emplace(123, &destroyed);
  }
  EXPECT_TRUE(destroyed);
}

} // namespace base
