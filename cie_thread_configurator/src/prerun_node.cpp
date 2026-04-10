#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>

#include "cie_config_msgs/msg/callback_group_info.hpp"
#include "cie_config_msgs/msg/non_ros_thread_info.hpp"
#include "rclcpp/rclcpp.hpp"
#include "yaml-cpp/yaml.h"

#include "cie_thread_configurator/cie_thread_configurator.hpp"
#include "cie_thread_configurator/prerun_node.hpp"

PrerunNode::PrerunNode(const rclcpp::NodeOptions &options)
    : Node("prerun_node", options) {
  auto cbg_qos = rclcpp::QoS(rclcpp::KeepAll()).reliable().transient_local();
  subscription_ =
      this->create_subscription<cie_config_msgs::msg::CallbackGroupInfo>(
          "/cie_thread_configurator/callback_group_info", cbg_qos,
          std::bind(&PrerunNode::callback_group_callback, this,
                    std::placeholders::_1));

  // volatile: publisher context in spawn_non_ros2_thread is destroyed after
  // publish, so transient_local is ineffective.
  auto non_ros_thread_qos = rclcpp::QoS(rclcpp::KeepAll()).reliable();
  non_ros_thread_subscription_ =
      this->create_subscription<cie_config_msgs::msg::NonRosThreadInfo>(
          "/cie_thread_configurator/non_ros_thread_info", non_ros_thread_qos,
          std::bind(&PrerunNode::non_ros_thread_callback, this,
                    std::placeholders::_1));
}

void PrerunNode::callback_group_callback(
    const cie_config_msgs::msg::CallbackGroupInfo::SharedPtr msg) {
  if (callback_group_ids_.find(msg->callback_group_id) !=
      callback_group_ids_.end()) {
    RCLCPP_ERROR(this->get_logger(),
                 "Duplicate callback_group_id received: tid=%ld | %s",
                 msg->thread_id, msg->callback_group_id.c_str());
    return;
  }

  RCLCPP_INFO(this->get_logger(), "Received CallbackGroupInfo: tid=%ld | %s",
              msg->thread_id, msg->callback_group_id.c_str());

  callback_group_ids_.insert(msg->callback_group_id);
}

void PrerunNode::non_ros_thread_callback(
    const cie_config_msgs::msg::NonRosThreadInfo::SharedPtr msg) {
  if (non_ros_thread_names_.find(msg->thread_name) !=
      non_ros_thread_names_.end()) {
    RCLCPP_ERROR(this->get_logger(),
                 "Duplicate thread_name received: tid=%ld | %s", msg->thread_id,
                 msg->thread_name.c_str());
    return;
  }

  RCLCPP_INFO(this->get_logger(), "Received NonRosThreadInfo: tid=%ld | %s",
              msg->thread_id, msg->thread_name.c_str());

  non_ros_thread_names_.insert(msg->thread_name);
}

void PrerunNode::dump_yaml_config(std::filesystem::path path) {
  YAML::Emitter out;

  out << YAML::BeginMap;

  // Add hardware information section
  out << YAML::Key << "hardware_info";
  out << YAML::Value << YAML::BeginMap;

  auto hw_info = cie_thread_configurator::get_hardware_info();

  for (const auto &[key, value] : hw_info) {
    out << YAML::Key << key << YAML::Value << value;
  }

  out << YAML::EndMap;

  // Add callback_groups section
  out << YAML::Key << "callback_groups";
  out << YAML::Value << YAML::BeginSeq;

  for (const std::string &callback_group_id : callback_group_ids_) {
    out << YAML::BeginMap;
    out << YAML::Key << "id" << YAML::Value << callback_group_id;
    out << YAML::Key << "affinity" << YAML::Value << YAML::Null;
    out << YAML::Key << "policy" << YAML::Value << "SCHED_OTHER";
    out << YAML::Key << "priority" << YAML::Value << 0;
    out << YAML::EndMap;
    out << YAML::Newline;
  }

  out << YAML::EndSeq;

  // Add non_ros_threads section
  out << YAML::Key << "non_ros_threads";
  out << YAML::Value << YAML::BeginSeq;

  for (const std::string &thread_name : non_ros_thread_names_) {
    out << YAML::BeginMap;
    out << YAML::Key << "id" << YAML::Value << thread_name;
    out << YAML::Key << "affinity" << YAML::Value << YAML::Null;
    out << YAML::Key << "policy" << YAML::Value << "SCHED_OTHER";
    out << YAML::Key << "priority" << YAML::Value << 0;
    out << YAML::EndMap;
    out << YAML::Newline;
  }

  out << YAML::EndSeq;
  out << YAML::EndMap;

  std::ofstream fout(path / "template.yaml");
  fout << out.c_str();
  fout.close();

  std::cout << "template.yaml is created in the current directory" << std::endl;
}
