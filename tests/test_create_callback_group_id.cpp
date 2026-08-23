#include <gtest/gtest.h>
#include <string>
#include <set>

#include "cie_thread_configurator/cie_thread_configurator.hpp"

TEST(CreateCallbackGroupId, DeterministicAndDistinct) {
  using ::cie_thread_configurator::create_callback_group_id;
  // Exercise the pure string-ID helper with simple inputs.
  // Parameter types follow the declaration in the project header.
  auto id1 = create_callback_group_id("v1_0", "v1_1");
  auto id2 = create_callback_group_id("v1_0", "v1_1");
  auto id3 = create_callback_group_id("v2_0", "v2_1");
  EXPECT_EQ(id1, id2);
  EXPECT_NE(id1, id3);
  EXPECT_FALSE(id1.empty());
}
