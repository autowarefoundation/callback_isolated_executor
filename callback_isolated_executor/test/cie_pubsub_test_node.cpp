#include <chrono>
#include <memory>

#include "rclcpp/rclcpp.hpp"
#include "std_msgs/msg/int32.hpp"

#include "callback_isolated_executor/callback_isolated_executor.hpp"

using namespace std::chrono_literals;

class TalkerNode : public rclcpp::Node {
public:
  TalkerNode() : Node("cie_test_talker"), count_(0) {
    publisher_ = create_publisher<std_msgs::msg::Int32>("chatter", 10);
    timer_group_ =
        create_callback_group(rclcpp::CallbackGroupType::MutuallyExclusive);
    timer_ = create_wall_timer(
        100ms,
        [this]() {
          auto message = std_msgs::msg::Int32();
          message.data = count_++;
          publisher_->publish(message);
        },
        timer_group_);
  }

private:
  rclcpp::Publisher<std_msgs::msg::Int32>::SharedPtr publisher_;
  rclcpp::TimerBase::SharedPtr timer_;
  rclcpp::CallbackGroup::SharedPtr timer_group_;
  int32_t count_;
};

class ListenerNode : public rclcpp::Node {
public:
  ListenerNode() : Node("cie_test_listener") {
    echo_publisher_ = create_publisher<std_msgs::msg::Int32>("echo", 10);
    sub_group_ =
        create_callback_group(rclcpp::CallbackGroupType::MutuallyExclusive);
    rclcpp::SubscriptionOptions sub_options;
    sub_options.callback_group = sub_group_;
    subscription_ = create_subscription<std_msgs::msg::Int32>(
        "chatter", 10,
        [this](const std_msgs::msg::Int32::SharedPtr msg) {
          echo_publisher_->publish(*msg);
        },
        sub_options);
  }

private:
  rclcpp::Subscription<std_msgs::msg::Int32>::SharedPtr subscription_;
  rclcpp::Publisher<std_msgs::msg::Int32>::SharedPtr echo_publisher_;
  rclcpp::CallbackGroup::SharedPtr sub_group_;
};

int main(int argc, char *argv[]) {
  // rclcpp::init() must precede constructing CallbackIsolatedExecutor: its
  // constructor creates an internal publisher node.
  rclcpp::init(argc, argv);
  {
    auto talker = std::make_shared<TalkerNode>();
    auto listener = std::make_shared<ListenerNode>();
    auto executor = std::make_shared<CallbackIsolatedExecutor>();
    executor->add_node(talker);
    executor->add_node(listener);
    // Blocks until SIGINT (launch_testing teardown) flips rclcpp::ok() false.
    executor->spin();
  }
  rclcpp::shutdown();
  return 0;
}
