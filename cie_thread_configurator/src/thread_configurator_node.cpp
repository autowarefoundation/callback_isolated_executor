#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

#include <error.h>
#include <sys/resource.h>
#include <sys/time.h>
#include <unistd.h>

#include "cie_config_msgs/msg/callback_group_info.hpp"
#include "cie_config_msgs/msg/non_ros_thread_info.hpp"
#include "rclcpp/rclcpp.hpp"
#include "yaml-cpp/yaml.h"

#include "cie_thread_configurator/cie_thread_configurator.hpp"
#include "cie_thread_configurator/sched_deadline.hpp"
#include "cie_thread_configurator/thread_configurator_node.hpp"

ThreadConfiguratorNode::ThreadConfiguratorNode(
    const rclcpp::NodeOptions &options)
    : Node("thread_configurator_node", options), unapplied_num_(0),
      cgroup_num_(0) {
  this->declare_parameter<std::string>("config_file", "");
  std::string config_file = this->get_parameter("config_file").as_string();

  if (config_file.empty()) {
    throw std::runtime_error(
        "'config_file' parameter must be provided (e.g. --ros-args -p "
        "config_file:=/path/to/config.yaml)");
  }

  YAML::Node yaml;
  try {
    yaml = YAML::LoadFile(config_file);
  } catch (const std::exception &e) {
    throw std::runtime_error("Error reading the YAML file '" + config_file +
                             "': " + e.what());
  }

  RCLCPP_INFO(this->get_logger(), "Loaded config from: %s",
              config_file.c_str());

  validate_hardware_info(yaml);

  YAML::Node callback_groups = yaml["callback_groups"];
  YAML::Node non_ros_threads = yaml["non_ros_threads"];

  size_t callback_groups_size = callback_groups ? callback_groups.size() : 0;
  size_t non_ros_threads_size = non_ros_threads ? non_ros_threads.size() : 0;

  unapplied_num_ = callback_groups_size + non_ros_threads_size;
  thread_configs_.resize(callback_groups_size + non_ros_threads_size);

  // For backward compatibility: remove trailing "Waitable@"s
  auto remove_trailing_waitable = [](std::string s) {
    static constexpr std::string_view suffix = "@Waitable";
    const std::size_t suffix_size = suffix.size();
    std::size_t s_size = s.size();

    while (s_size >= suffix_size &&
           std::char_traits<char>::compare(s.data() + (s_size - suffix_size),
                                           suffix.data(), suffix_size) == 0) {
      s_size -= suffix_size;
    }
    s.resize(s_size);

    return s;
  };

  static const std::unordered_set<std::string> valid_policies = {
      "SCHED_OTHER", "SCHED_BATCH", "SCHED_IDLE",
      "SCHED_FIFO",  "SCHED_RR",    "SCHED_DEADLINE",
  };

  // Common lambda to load thread configuration from YAML
  auto load_thread_config = [](ThreadConfig &config, const YAML::Node &node) {
    config.thread_str = node["id"].as<std::string>();
    for (auto &cpu : node["affinity"])
      config.affinity.push_back(cpu.as<int>());
    config.policy = node["policy"].as<std::string>();

    if (valid_policies.find(config.policy) == valid_policies.end()) {
      throw std::runtime_error("Unknown scheduling policy '" + config.policy +
                               "' for id=" + config.thread_str);
    }

    if (config.policy == "SCHED_DEADLINE") {
      config.runtime = node["runtime"].as<unsigned int>();
      config.period = node["period"].as<unsigned int>();
      config.deadline = node["deadline"].as<unsigned int>();
    } else {
      config.priority = node["priority"].as<int>();
    }
  };

  size_t config_index = 0;

  // Load callback_groups configuration
  for (size_t i = 0; i < callback_groups_size; i++) {
    auto &config = thread_configs_[config_index++];
    load_thread_config(config, callback_groups[i]);
    config.thread_str = remove_trailing_waitable(config.thread_str);
    id_to_thread_config_[config.thread_str] = &config;
  }

  // Load non_ros_threads configuration
  for (size_t i = 0; i < non_ros_threads_size; i++) {
    auto &config = thread_configs_[config_index++];
    load_thread_config(config, non_ros_threads[i]);
    id_to_thread_config_[config.thread_str] = &config;
  }

  auto cbg_qos = rclcpp::QoS(rclcpp::KeepAll()).reliable().transient_local();
  subscription_ =
      this->create_subscription<cie_config_msgs::msg::CallbackGroupInfo>(
          "/cie_thread_configurator/callback_group_info", cbg_qos,
          std::bind(&ThreadConfiguratorNode::callback_group_callback, this,
                    std::placeholders::_1));

  // volatile: publisher context in spawn_non_ros2_thread is destroyed after
  // publish, so transient_local is ineffective.
  auto non_ros_thread_qos = rclcpp::QoS(rclcpp::KeepAll()).reliable();
  non_ros_thread_subscription_ =
      this->create_subscription<cie_config_msgs::msg::NonRosThreadInfo>(
          "/cie_thread_configurator/non_ros_thread_info", non_ros_thread_qos,
          std::bind(&ThreadConfiguratorNode::non_ros_thread_callback, this,
                    std::placeholders::_1));

  if (unapplied_num_ == 0) {
    on_all_configured();
  }
}

void ThreadConfiguratorNode::validate_hardware_info(const YAML::Node &yaml) {
  if (!yaml["hardware_info"]) {
    RCLCPP_WARN(this->get_logger(),
                "No hardware_info section found in configuration file. "
                "Skipping hardware validation.");
    return;
  }

  YAML::Node yaml_hw_info = yaml["hardware_info"];
  auto current_hw_info = cie_thread_configurator::get_hardware_info();

  bool all_match = true;
  std::vector<std::string> mismatches;

  for (const auto &[key, current_value] : current_hw_info) {
    if (!yaml_hw_info[key]) {
      continue;
    }

    std::string yaml_value = yaml_hw_info[key].as<std::string>();
    if (yaml_value != current_value) {
      all_match = false;
      mismatches.push_back(key + ": expected '" + yaml_value + "', got '" +
                           current_value + "'");
    }
  }

  if (!all_match) {
    std::string msg =
        "Hardware validation failed with the following mismatches:";
    for (const auto &mismatch : mismatches) {
      msg += "\n  - " + mismatch;
    }
    throw std::runtime_error(msg);
  }

  RCLCPP_INFO(this->get_logger(),
              "Hardware validation successful. Configuration matches this "
              "system.");
}

ThreadConfiguratorNode::~ThreadConfiguratorNode() {
  if (cgroup_num_ > 0) {
    for (int i = 0; i < cgroup_num_; i++) {
      rmdir(("/sys/fs/cgroup/cpuset/" + std::to_string(i)).c_str());
    }
  }
}

void ThreadConfiguratorNode::print_all_unapplied() {
  if (unapplied_num_ == 0) {
    return;
  }

  RCLCPP_WARN(this->get_logger(), "Following threads are not yet configured");

  for (auto &config : thread_configs_) {
    if (!config.applied) {
      RCLCPP_WARN(this->get_logger(), "  - %s", config.thread_str.c_str());
    }
  }
}

bool ThreadConfiguratorNode::set_affinity_by_cgroup(
    int64_t thread_id, const std::vector<int> &cpus) {
  std::string cgroup_path =
      "/sys/fs/cgroup/cpuset/" + std::to_string(cgroup_num_++);
  if (!std::filesystem::create_directory(cgroup_path))
    return false;

  std::string cpus_path = cgroup_path + "/cpuset.cpus";
  if (std::ofstream cpus_file{cpus_path}) {
    for (size_t i = 0; i < cpus.size(); i++) {
      if (i > 0)
        cpus_file << ",";
      cpus_file << cpus[i];
    }
  } else {
    return false;
  }

  std::string mems_path = cgroup_path + "/cpuset.mems";
  if (std::ofstream mems_file{mems_path}) {
    mems_file << 0;
  } else {
    return false;
  }

  std::string tasks_path = cgroup_path + "/tasks";
  if (std::ofstream tasks_file{tasks_path}) {
    tasks_file << thread_id;
  } else {
    return false;
  }

  return true;
}

bool ThreadConfiguratorNode::issue_syscalls(const ThreadConfig &config) {
  if (config.policy == "SCHED_OTHER" || config.policy == "SCHED_BATCH" ||
      config.policy == "SCHED_IDLE") {
    struct sched_param param;
    param.sched_priority = 0;

    static std::unordered_map<std::string, int> m = {
        {"SCHED_OTHER", SCHED_OTHER},
        {"SCHED_BATCH", SCHED_BATCH},
        {"SCHED_IDLE", SCHED_IDLE},
    };

    if (sched_setscheduler(config.thread_id, m[config.policy], &param) == -1) {
      RCLCPP_ERROR(
          this->get_logger(), "Failed to configure policy (id=%s, tid=%ld): %s",
          config.thread_str.c_str(), config.thread_id, strerror(errno));
      return false;
    }

    // Specify nice value
    if (setpriority(PRIO_PROCESS, config.thread_id, config.priority) == -1) {
      RCLCPP_ERROR(this->get_logger(),
                   "Failed to configure nice value (id=%s, tid=%ld): %s",
                   config.thread_str.c_str(), config.thread_id,
                   strerror(errno));
      return false;
    }

  } else if (config.policy == "SCHED_FIFO" || config.policy == "SCHED_RR") {
    struct sched_param param;
    param.sched_priority = config.priority;

    static std::unordered_map<std::string, int> m = {
        {"SCHED_FIFO", SCHED_FIFO},
        {"SCHED_RR", SCHED_RR},
    };

    if (sched_setscheduler(config.thread_id, m[config.policy], &param) == -1) {
      RCLCPP_ERROR(
          this->get_logger(), "Failed to configure policy (id=%s, tid=%ld): %s",
          config.thread_str.c_str(), config.thread_id, strerror(errno));
      return false;
    }

  } else if (config.policy == "SCHED_DEADLINE") {
    struct sched_attr attr;
    memset(&attr, 0, sizeof(attr));
    attr.size = sizeof(attr);
    // SCHED_FLAG_RESET_ON_FORK lets the target thread still call
    // fork(2)/clone(2) after being placed under SCHED_DEADLINE; without it,
    // clone(2) returns EAGAIN. Children reset to SCHED_OTHER; each
    // callback-group thread that needs its own SCHED_DEADLINE gets it via its
    // own CallbackGroupInfo message.
    attr.sched_flags = SCHED_FLAG_RESET_ON_FORK;
    attr.sched_nice = 0;
    attr.sched_priority = 0;

    attr.sched_policy = SCHED_DEADLINE;
    attr.sched_runtime = config.runtime;
    attr.sched_period = config.period;
    attr.sched_deadline = config.deadline;

    if (sched_setattr(config.thread_id, &attr, 0) == -1) {
      RCLCPP_ERROR(
          this->get_logger(), "Failed to configure policy (id=%s, tid=%ld): %s",
          config.thread_str.c_str(), config.thread_id, strerror(errno));
      return false;
    }
  } else {
    RCLCPP_ERROR(
        this->get_logger(), "Unknown scheduling policy '%s' (id=%s, tid=%ld)",
        config.policy.c_str(), config.thread_str.c_str(), config.thread_id);
    return false;
  }

  if (config.affinity.size() > 0) {
    if (config.policy == "SCHED_DEADLINE") {
      if (!set_affinity_by_cgroup(config.thread_id, config.affinity)) {
        RCLCPP_ERROR(this->get_logger(),
                     "Failed to configure affinity (id=%s, tid=%ld): %s",
                     config.thread_str.c_str(), config.thread_id,
                     "Please disable cgroup v2 if used: "
                     "`systemd.unified_cgroup_hierarchy=0`");
        return false;
      }
    } else {
      cpu_set_t set;
      CPU_ZERO(&set);
      for (int cpu : config.affinity)
        CPU_SET(cpu, &set);
      if (sched_setaffinity(config.thread_id, sizeof(set), &set) == -1) {
        RCLCPP_ERROR(this->get_logger(),
                     "Failed to configure affinity (id=%s, tid=%ld): %s",
                     config.thread_str.c_str(), config.thread_id,
                     strerror(errno));
        return false;
      }
    }
  }

  return true;
}

void ThreadConfiguratorNode::callback_group_callback(
    const cie_config_msgs::msg::CallbackGroupInfo::SharedPtr msg) {
  auto it = id_to_thread_config_.find(msg->callback_group_id);
  if (it == id_to_thread_config_.end()) {
    RCLCPP_INFO(this->get_logger(),
                "Received CallbackGroupInfo: but the yaml file does not "
                "contain configuration for id=%s (tid=%ld)",
                msg->callback_group_id.c_str(), msg->thread_id);
    return;
  }

  ThreadConfig *config = it->second;
  if (config->applied) {
    // Always re-apply: the OS may reuse the same thread IDs after an
    // application restarts, so we cannot use thread_id equality to skip
    // reconfiguration.
    RCLCPP_INFO(
        this->get_logger(),
        "Re-applying configuration for already configured callback group "
        "(id=%s, tid=%ld)",
        msg->callback_group_id.c_str(), msg->thread_id);
  }

  RCLCPP_INFO(this->get_logger(), "Received CallbackGroupInfo: tid=%ld | %s",
              msg->thread_id, msg->callback_group_id.c_str());
  config->thread_id = msg->thread_id;

  if (!issue_syscalls(*config)) {
    RCLCPP_WARN(this->get_logger(),
                "Skipping configuration for callback group (id=%s, tid=%ld) "
                "due to syscall failure.",
                msg->callback_group_id.c_str(), msg->thread_id);
    return;
  }

  if (!config->applied) {
    unapplied_num_--;
  }
  config->applied = true;

  if (unapplied_num_ == 0 && !configured_at_least_once_) {
    on_all_configured();
  }
}

void ThreadConfiguratorNode::non_ros_thread_callback(
    const cie_config_msgs::msg::NonRosThreadInfo::SharedPtr msg) {
  auto it = id_to_thread_config_.find(msg->thread_name);
  if (it == id_to_thread_config_.end()) {
    RCLCPP_INFO(this->get_logger(),
                "Received NonRosThreadInfo: but the yaml file does not "
                "contain configuration for name=%s (tid=%ld)",
                msg->thread_name.c_str(), msg->thread_id);
    return;
  }

  ThreadConfig *config = it->second;
  if (config->applied) {
    // Always re-apply: the OS may reuse the same thread IDs after an
    // application restarts, so we cannot use thread_id equality to skip
    // reconfiguration.
    RCLCPP_INFO(
        this->get_logger(),
        "Re-applying configuration for already configured non-ROS thread "
        "(name=%s, tid=%ld)",
        msg->thread_name.c_str(), msg->thread_id);
  }

  RCLCPP_INFO(this->get_logger(), "Received NonRosThreadInfo: tid=%ld | %s",
              msg->thread_id, msg->thread_name.c_str());
  config->thread_id = msg->thread_id;

  if (!issue_syscalls(*config)) {
    RCLCPP_WARN(this->get_logger(),
                "Skipping configuration for non-ROS thread (name=%s, tid=%ld) "
                "due to syscall failure.",
                msg->thread_name.c_str(), msg->thread_id);
    return;
  }

  if (!config->applied) {
    unapplied_num_--;
  }
  config->applied = true;

  if (unapplied_num_ == 0 && !configured_at_least_once_) {
    on_all_configured();
  }
}

void ThreadConfiguratorNode::on_all_configured() {
  RCLCPP_INFO(this->get_logger(),
              "Success: All of the configurations are applied.");

  configured_at_least_once_ = true;
}

bool ThreadConfiguratorNode::has_cgroup() const { return cgroup_num_ > 0; }
