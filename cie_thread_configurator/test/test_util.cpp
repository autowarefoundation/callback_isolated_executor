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
#include "std_srvs/srv/empty.hpp"
#include "gtest/gtest.h"

#include "cie_thread_configurator/cie_thread_configurator.hpp"

using cie_thread_configurator::create_callback_group_id;

// Each test runs in its own rclcpp context so node names and the ROS graph
// do not leak between cases.
class CreateCallbackGroupIdTest : public ::testing::Test {
protected:
  void SetUp() override { rclcpp::init(0, nullptr); }
  void TearDown() override { rclcpp::shutdown(); }

  rclcpp::CallbackGroup::SharedPtr
  make_group(const rclcpp::Node::SharedPtr &node) {
    return node->create_callback_group(
        rclcpp::CallbackGroupType::MutuallyExclusive);
  }

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

  rclcpp::ServiceBase::SharedPtr
  add_service(const rclcpp::Node::SharedPtr &node,
              const rclcpp::CallbackGroup::SharedPtr &group,
              const std::string &name) {
    return node->create_service<std_srvs::srv::Empty>(
        name,
        [](const std::shared_ptr<std_srvs::srv::Empty::Request>,
           std::shared_ptr<std_srvs::srv::Empty::Response>) {},
        rclcpp::ServicesQoS().get_rmw_qos_profile(), group);
  }

  rclcpp::ClientBase::SharedPtr
  add_client(const rclcpp::Node::SharedPtr &node,
             const rclcpp::CallbackGroup::SharedPtr &group,
             const std::string &name) {
    return node->create_client<std_srvs::srv::Empty>(
        name, rclcpp::ServicesQoS().get_rmw_qos_profile(), group);
  }

  rclcpp::TimerBase::SharedPtr
  add_timer(const rclcpp::Node::SharedPtr &node,
            const rclcpp::CallbackGroup::SharedPtr &group,
            std::chrono::nanoseconds period) {
    return node->create_wall_timer(period, []() {}, group);
  }
};

// A node at the root namespace must produce a single leading slash
// (e.g. "/node", not "//node"), then the entity descriptor.
TEST_F(CreateCallbackGroupIdTest, RootNamespaceWithSubscription) {
  // Arrange
  auto node = std::make_shared<rclcpp::Node>("test_node");
  auto group = make_group(node);
  auto sub = add_subscription(node, group, "/chatter");
  ASSERT_NE(sub, nullptr);

  // Act
  const std::string id = create_callback_group_id(group, node);

  // Assert
  EXPECT_EQ(id, "/test_node@Subscription(/chatter)");
}

// A non-root namespace exercises the "ns + '/'" branch.
TEST_F(CreateCallbackGroupIdTest, NonRootNamespace) {
  // Arrange
  auto node = std::make_shared<rclcpp::Node>("test_node", "my_ns");
  auto group = make_group(node);
  auto sub = add_subscription(node, group, "/chatter");
  ASSERT_NE(sub, nullptr);

  // Act
  const std::string id = create_callback_group_id(group, node);

  // Assert
  EXPECT_EQ(id, "/my_ns/test_node@Subscription(/chatter)");
}

// A service entity is rendered as Service(<name>).
TEST_F(CreateCallbackGroupIdTest, ServiceEntity) {
  // Arrange
  auto node = std::make_shared<rclcpp::Node>("test_node");
  auto group = make_group(node);
  auto service = add_service(node, group, "/srv");
  ASSERT_NE(service, nullptr);

  // Act
  const std::string id = create_callback_group_id(group, node);

  // Assert
  EXPECT_EQ(id, "/test_node@Service(/srv)");
}

// A client entity is rendered as Client(<name>).
TEST_F(CreateCallbackGroupIdTest, ClientEntity) {
  // Arrange
  auto node = std::make_shared<rclcpp::Node>("test_node");
  auto group = make_group(node);
  auto client = add_client(node, group, "/srv");
  ASSERT_NE(client, nullptr);

  // Act
  const std::string id = create_callback_group_id(group, node);

  // Assert
  EXPECT_EQ(id, "/test_node@Client(/srv)");
}

// A timer has no name, so it is identified by its period in nanoseconds.
// 100 ms == 100000000 ns.
TEST_F(CreateCallbackGroupIdTest, TimerEntityUsesPeriodNanoseconds) {
  // Arrange
  auto node = std::make_shared<rclcpp::Node>("test_node");
  auto group = make_group(node);
  auto timer = add_timer(node, group, std::chrono::milliseconds(100));
  ASSERT_NE(timer, nullptr);

  // Act
  const std::string id = create_callback_group_id(group, node);

  // Assert
  EXPECT_EQ(id, "/test_node@Timer(100000000)");
}

// Multiple entities are joined with '@' and sorted alphabetically, regardless
// of the order collect_all_ptrs reports them (subscriptions, services,
// clients, then timers). Sorting puts them in the order Client < Service <
// Subscription < Timer.
TEST_F(CreateCallbackGroupIdTest, MultipleEntitiesAreSortedAlphabetically) {
  // Arrange
  auto node = std::make_shared<rclcpp::Node>("test_node");
  auto group = make_group(node);
  auto sub = add_subscription(node, group, "/chatter");
  auto timer = add_timer(node, group, std::chrono::milliseconds(100));
  auto service = add_service(node, group, "/srv");
  auto client = add_client(node, group, "/cli");
  ASSERT_NE(sub, nullptr);
  ASSERT_NE(timer, nullptr);
  ASSERT_NE(service, nullptr);
  ASSERT_NE(client, nullptr);

  // Act
  const std::string id = create_callback_group_id(group, node);

  // Assert
  EXPECT_EQ(id, "/test_node@Client(/cli)@Service(/srv)@Subscription(/"
                "chatter)@Timer(100000000)");
}

// The id must depend only on the set of entities, not on the order they were
// registered. Two groups holding the same subscriptions registered in opposite
// orders must produce identical ids.
TEST_F(CreateCallbackGroupIdTest, IdIsIndependentOfRegistrationOrder) {
  // Arrange
  auto node = std::make_shared<rclcpp::Node>("test_node");

  auto group_ascending = make_group(node);
  auto sub_a1 = add_subscription(node, group_ascending, "/a");
  auto sub_b1 = add_subscription(node, group_ascending, "/b");
  ASSERT_NE(sub_a1, nullptr);
  ASSERT_NE(sub_b1, nullptr);

  auto group_descending = make_group(node);
  auto sub_b2 = add_subscription(node, group_descending, "/b");
  auto sub_a2 = add_subscription(node, group_descending, "/a");
  ASSERT_NE(sub_b2, nullptr);
  ASSERT_NE(sub_a2, nullptr);

  // Act
  const std::string id_ascending =
      create_callback_group_id(group_ascending, node);
  const std::string id_descending =
      create_callback_group_id(group_descending, node);

  // Assert
  EXPECT_EQ(id_ascending, id_descending);
  EXPECT_EQ(id_ascending, "/test_node@Subscription(/a)@Subscription(/b)");
}

// With no entities, only the bare "node@" prefix is built, so stripping the
// trailing '@' must yield exactly the node identifier and nothing else.
TEST_F(CreateCallbackGroupIdTest, EmptyGroupStripsTrailingSeparator) {
  // Arrange
  auto node = std::make_shared<rclcpp::Node>("test_node");
  auto group = make_group(node);

  // Act
  const std::string id = create_callback_group_id(group, node);

  // Assert
  EXPECT_EQ(id, "/test_node");
}

// The Node overload must delegate to the NodeBaseInterface overload and yield
// an identical result.
TEST_F(CreateCallbackGroupIdTest,
       NodeBaseInterfaceOverloadMatchesNodeOverload) {
  // Arrange
  auto node = std::make_shared<rclcpp::Node>("test_node");
  auto group = make_group(node);
  auto sub = add_subscription(node, group, "/chatter");
  ASSERT_NE(sub, nullptr);

  // Act
  const std::string id_base =
      create_callback_group_id(group, node->get_node_base_interface());
  const std::string id_node = create_callback_group_id(group, node);

  // Assert
  EXPECT_EQ(id_base, id_node);
  EXPECT_EQ(id_base, "/test_node@Subscription(/chatter)");
}
