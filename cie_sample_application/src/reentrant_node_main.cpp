#include <chrono>

#include "callback_isolated_executor/callback_isolated_executor.hpp"
#include "cie_sample_application/reentrant_node.hpp"

int main(int argc, char *argv[]) {
  rclcpp::init(argc, argv);

  auto node = std::make_shared<ReentrantNode>();
  auto executor = std::make_shared<CallbackIsolatedExecutor>(
      rclcpp::ExecutorOptions(), 4, false, std::chrono::nanoseconds(-1));

  executor->add_node(node);
  executor->spin();

  rclcpp::shutdown();
  return 0;
}
