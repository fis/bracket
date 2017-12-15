#include "base/enumarray.h"
#include "gtest/gtest.h"

namespace base {

enum TestEnum {
  KEY_A,
  KEY_B,
  KEY_C,
  KEY_D = 4,
};

TEST(EnumArrayTest, Lookup) {
  EnumArray<TestEnum, int, 5> array(
      {
        { KEY_B, 'b' },
        { KEY_A, 'a' },
        { KEY_D, 'd' },
      }, 'x');

  EXPECT_EQ(array.size(), 5u);
  EXPECT_EQ(array[KEY_A], 'a');
  EXPECT_EQ(array[KEY_B], 'b');
  EXPECT_EQ(array[KEY_C], 'x');
  EXPECT_EQ(array[KEY_D], 'd');
}

} // namespace base
