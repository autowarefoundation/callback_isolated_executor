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
    int priority;

    // For SCHED_DEADLINE
    unsigned int runtime;
    unsigned int period;
    unsigned int deadline;

    bool applied = false;
  };

public:
  explicit ThreadConfiguratorNode(const YAML::Node &yaml);
  ~ThreadConfiguratorNode();
  void print_all_unapplied();
  bool has_configured_once() const;

private:
  bool set_affinity_by_cgroup(int64_t thread_id, const std::vector<int> &cpus);
  bool issue_syscalls(const ThreadConfig &config);
  void callback_group_callback(
      const cie_config_msgs::msg::CallbackGroupInfo::SharedPtr msg);
  void non_ros_thread_callback(
      const cie_config_msgs::msg::NonRosThreadInfo::SharedPtr msg);
  void apply_deadline_configs();

  rclcpp::Subscription<cie_config_msgs::msg::CallbackGroupInfo>::SharedPtr
      subscription_;
  rclcpp::Subscription<cie_config_msgs::msg::NonRosThreadInfo>::SharedPtr
      non_ros_thread_subscription_;

  std::vector<ThreadConfig> thread_configs_;
  std::unordered_map<std::string, ThreadConfig *> id_to_thread_config_;
  int unapplied_num_;
  int cgroup_num_;
  bool configured_at_least_once_ = false;

  std::vector<ThreadConfig> deadline_configs_;
};
