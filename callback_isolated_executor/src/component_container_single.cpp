// A single-threaded component container that publishes its thread ID to the
// CIE thread configurator. This exists for research purposes, to support the
// use case where multiple callback groups run on a single thread while still
// allowing thread scheduling parameters (e.g., priority, CPU affinity) to be
// configured via the thread configurator.

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <sys/syscall.h>

#include "rclcpp/rclcpp.hpp"
#include "rclcpp_components/component_manager.hpp"

#include "cie_thread_configurator/cie_thread_configurator.hpp"

int main(int argc, char *argv[]) {
  const char *ret = getenv("THREAD_CONFIGURATION_ID");
  if (ret == nullptr) {
    std::cerr << "THREAD_CONFIGURATION_ID environment variable is needed"
              << std::endl;
    return -1;
  }

  rclcpp::init(argc, argv);

  std::string config_id = ret;

  auto exec = std::make_shared<rclcpp::executors::SingleThreadedExecutor>();
  auto node = std::make_shared<rclcpp_components::ComponentManager>(exec);
  exec->add_node(node);

  auto config_publisher = cie_thread_configurator::create_client_publisher();
  auto tid = syscall(SYS_gettid);

  // dirty
  for (int i = 0; i < 10; i++) {
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    cie_thread_configurator::publish_callback_group_info(config_publisher, tid,
                                                         config_id);
  }

  exec->spin();
}
