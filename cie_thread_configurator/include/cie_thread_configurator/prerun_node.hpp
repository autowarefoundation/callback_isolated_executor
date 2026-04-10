#pragma once

#include <filesystem>
#include <set>
#include <string>

#include "rclcpp/rclcpp.hpp"
#include "yaml-cpp/yaml.h"

#include "cie_config_msgs/msg/callback_group_info.hpp"
#include "cie_config_msgs/msg/non_ros_thread_info.hpp"

class PrerunNode : public rclcpp::Node {
public:
  explicit PrerunNode(
      const rclcpp::NodeOptions &options = rclcpp::NodeOptions());
  void dump_yaml_config(std::filesystem::path path);

private:
  void callback_group_callback(
      const cie_config_msgs::msg::CallbackGroupInfo::SharedPtr msg);
  void non_ros_thread_callback(
      const cie_config_msgs::msg::NonRosThreadInfo::SharedPtr msg);

  rclcpp::Subscription<cie_config_msgs::msg::CallbackGroupInfo>::SharedPtr
      subscription_;
  rclcpp::Subscription<cie_config_msgs::msg::NonRosThreadInfo>::SharedPtr
      non_ros_thread_subscription_;
  std::set<std::string> callback_group_ids_;
  std::set<std::string> non_ros_thread_names_;
};
