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

    while (rclcpp::ok() && !node->all_applied()) {
      executor->spin_once();
    }

    if (node->all_applied()) {
      RCLCPP_INFO(node->get_logger(),
                  "Success: All of the configurations are applied.");
      if (node->has_cgroup()) {
        RCLCPP_INFO(node->get_logger(),
                    "Press enter to exit and remove the cgroups created for "
                    "thread affinity:");
        std::cin.get();
      }
    } else {
      node->print_all_unapplied();
    }
  } catch (const std::exception &e) {
    std::cerr << "[ERROR] " << e.what() << std::endl;
    rclcpp::shutdown();
    return 1;
  }

  rclcpp::shutdown();
  return 0;
}
