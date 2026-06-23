// Copyright 2026 The Autoware Contributors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <chrono>
#include <memory>
#include <string>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/string.hpp"
#include "gtest/gtest.h"

#include "cie_thread_configurator/cie_thread_configurator.hpp"

using cie_thread_configurator::create_callback_group_id;

// Each test runs in its own rclcpp context so node names and the ROS graph
// do not leak between cases.
class CreateCallbackGroupIdTest : public ::testing::Test {
protected:
  void SetUp() override { rclcpp::init(0, nullptr); }
  void TearDown() override { rclcpp::shutdown(); }

  // Helper: attach a subscription on an absolute topic to the given group.
  rclcpp::SubscriptionBase::SharedPtr
  add_subscription(const rclcpp::Node::SharedPtr &node,
                   const rclcpp::CallbackGroup::SharedPtr &group,
                   const std::string &topic) {
    rclcpp::SubscriptionOptions options;
    options.callback_group = group;
    return node->create_subscription<std_msgs::msg::String>(
        topic, rclcpp::QoS(10),
        [](const std_msgs::msg::String::ConstSharedPtr) {}, options);
  }
};

// A node at the root namespace must produce a single leading slash
// (e.g. "/node", not "//node"), then the entity descriptor.
TEST_F(CreateCallbackGroupIdTest, RootNamespaceWithSubscription) {
  auto node = std::make_shared<rclcpp::Node>("test_node");
  auto group =
      node->create_callback_group(rclcpp::CallbackGroupType::MutuallyExclusive);
  auto sub = add_subscription(node, group, "/chatter");
  ASSERT_NE(sub, nullptr);

  EXPECT_EQ(create_callback_group_id(group, node),
            "/test_node@Subscription(/chatter)");
}

// A non-root namespace exercises the "ns + '/'" branch.
TEST_F(CreateCallbackGroupIdTest, NonRootNamespace) {
  auto node = std::make_shared<rclcpp::Node>("test_node", "my_ns");
  auto group =
      node->create_callback_group(rclcpp::CallbackGroupType::MutuallyExclusive);
  auto sub = add_subscription(node, group, "/chatter");
  ASSERT_NE(sub, nullptr);

  EXPECT_EQ(create_callback_group_id(group, node),
            "/my_ns/test_node@Subscription(/chatter)");
}

// The trailing '@' separator appended after the last entity must be removed.
TEST_F(CreateCallbackGroupIdTest, TrailingSeparatorStripped) {
  auto node = std::make_shared<rclcpp::Node>("test_node");
  auto group =
      node->create_callback_group(rclcpp::CallbackGroupType::MutuallyExclusive);
  auto sub = add_subscription(node, group, "/chatter");
  ASSERT_NE(sub, nullptr);

  const std::string id = create_callback_group_id(group, node);
  ASSERT_FALSE(id.empty());
  EXPECT_NE(id.back(), '@');
}

// Timers have no name, so they are identified by their period in nanoseconds.
// 100 ms == 100000000 ns.
TEST_F(CreateCallbackGroupIdTest, TimerEntityUsesPeriodNanoseconds) {
  auto node = std::make_shared<rclcpp::Node>("test_node");
  auto group =
      node->create_callback_group(rclcpp::CallbackGroupType::MutuallyExclusive);
  auto timer =
      node->create_wall_timer(std::chrono::milliseconds(100), []() {}, group);
  ASSERT_NE(timer, nullptr);

  EXPECT_EQ(create_callback_group_id(group, node),
            "/test_node@Timer(100000000)");
}

// The Node overload must delegate to the NodeBaseInterface overload and yield
// an identical result.
TEST_F(CreateCallbackGroupIdTest,
       NodeBaseInterfaceOverloadMatchesNodeOverload) {
  auto node = std::make_shared<rclcpp::Node>("test_node");
  auto group =
      node->create_callback_group(rclcpp::CallbackGroupType::MutuallyExclusive);
  auto sub = add_subscription(node, group, "/chatter");
  ASSERT_NE(sub, nullptr);

  const std::string id_base =
      create_callback_group_id(group, node->get_node_base_interface());
  const std::string id_node = create_callback_group_id(group, node);

  EXPECT_EQ(id_base, id_node);
  EXPECT_EQ(id_base, "/test_node@Subscription(/chatter)");
}
