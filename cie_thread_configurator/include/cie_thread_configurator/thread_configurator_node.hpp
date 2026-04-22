#pragma once

#include <string>
#include <unordered_map>

#include "rclcpp/rclcpp.hpp"
#include "yaml-cpp/yaml.h"

#include "cie_config_msgs/msg/callback_group_info.hpp"
#include "cie_config_msgs/msg/non_ros_thread_info.hpp"

class ThreadConfiguratorNode : public rclcpp::Node {
  struct ThreadConfig {
    std::string thread_str; // callback_group_id or thread_name
    int64_t thread_id = -1;
    std::vector<int> affinity;
    std::string policy;
    int priority = 0;

    // For SCHED_DEADLINE
    unsigned int runtime = 0;
    unsigned int period = 0;
    unsigned int deadline = 0;

    bool applied = false;
  };

public:
  /// Construct the node. Reads the 'config_file' ROS parameter, loads the
  /// YAML, and performs hardware validation when a 'hardware_info' section
  /// is present in the configuration.
  /// @throws std::runtime_error if the 'config_file' parameter is empty, the
  ///         YAML file cannot be loaded, or a present hardware_info section
  ///         does not match the current system.
  explicit ThreadConfiguratorNode(
      const rclcpp::NodeOptions &options = rclcpp::NodeOptions());
  ~ThreadConfiguratorNode();
  void print_all_unapplied();

  bool has_cgroup() const;

private:
  void validate_hardware_info(const YAML::Node &yaml);
  bool set_affinity_by_cgroup(int64_t thread_id, const std::vector<int> &cpus);
  bool issue_syscalls(const ThreadConfig &config);
  void callback_group_callback(
      const cie_config_msgs::msg::CallbackGroupInfo::SharedPtr msg);
  void non_ros_thread_callback(
      const cie_config_msgs::msg::NonRosThreadInfo::SharedPtr msg);
  void on_all_configured();

  rclcpp::Subscription<cie_config_msgs::msg::CallbackGroupInfo>::SharedPtr
      subscription_;
  rclcpp::Subscription<cie_config_msgs::msg::NonRosThreadInfo>::SharedPtr
      non_ros_thread_subscription_;

  std::vector<ThreadConfig> thread_configs_;
  std::unordered_map<std::string, ThreadConfig *> id_to_thread_config_;
  int unapplied_num_;
  int cgroup_num_;
  bool configured_at_least_once_ = false;
};
