#include <exception>
#include <iostream>
#include <memory>

#include "rclcpp/rclcpp.hpp"

#include "cie_thread_configurator/thread_configurator_node.hpp"

int main(int argc, char *argv[]) {
  rclcpp::init(argc, argv);

  try {
    auto node = std::make_shared<ThreadConfiguratorNode>();
    auto executor =
        std::make_shared<rclcpp::executors::SingleThreadedExecutor>();

    executor->add_node(node);
    executor->spin();

    node->print_all_unapplied();
  } catch (const std::exception &e) {
    std::cerr << "[ERROR] " << e.what() << std::endl;
    rclcpp::shutdown();
    return 1;
  }

  rclcpp::shutdown();
  return 0;
}
