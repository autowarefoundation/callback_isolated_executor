// Copyright 2026 The Autoware Contributors
//
// Licensed under the Apache License, Version 2.0 (the "License");
// you may not use this file except in compliance with the License.
// You may obtain a copy of the License at
//
//     http://www.apache.org/licenses/LICENSE-2.0
//
// Unless required by applicable law or agreed to in writing, software
// distributed under the License is distributed on an "AS IS" BASIS,
// WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
// See the License for the specific language governing permissions and
// limitations under the License.

#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>

#include <unistd.h>

#include "rclcpp/rclcpp.hpp"
#include "gtest/gtest.h"

#include "cie_thread_configurator/thread_configurator_node.hpp"

// The YAML scheduling-parameter rules (policy-dependent 'nice' vs 'priority'
// keys and their ranges) are validated in the ThreadConfiguratorNode
// constructor, so each case writes a config file and constructs the node
// through its public API.
class ThreadConfiguratorNodeYamlTest : public ::testing::Test {
protected:
  void SetUp() override {
    rclcpp::init(0, nullptr);
    config_path_ =
        std::filesystem::temp_directory_path() /
        ("cie_thread_configurator_test_" + std::to_string(getpid()) + ".yaml");
  }

  void TearDown() override {
    rclcpp::shutdown();
    std::filesystem::remove(config_path_);
  }

  std::shared_ptr<ThreadConfiguratorNode>
  make_node_from_yaml(const std::string &yaml) {
    std::ofstream fout(config_path_);
    fout << yaml;
    fout.close();

    rclcpp::NodeOptions options;
    options.parameter_overrides({{"config_file", config_path_.string()}});
    return std::make_shared<ThreadConfiguratorNode>(options);
  }

  std::filesystem::path config_path_;
};

TEST_F(ThreadConfiguratorNodeYamlTest, AcceptsNiceForCfsPolicies) {
  for (const char *policy : {"SCHED_OTHER", "SCHED_BATCH", "SCHED_IDLE"}) {
    EXPECT_NO_THROW(make_node_from_yaml("callback_groups:\n"
                                        "  - id: my_cbg\n"
                                        "    policy: " +
                                        std::string(policy) +
                                        "\n"
                                        "    nice: -10\n"
                                        "    affinity: []\n"
                                        "non_ros_threads: []\n"))
        << policy;
  }
}

TEST_F(ThreadConfiguratorNodeYamlTest, AcceptsPriorityForRtPolicies) {
  for (const char *policy : {"SCHED_FIFO", "SCHED_RR"}) {
    EXPECT_NO_THROW(make_node_from_yaml("callback_groups:\n"
                                        "  - id: my_cbg\n"
                                        "    policy: " +
                                        std::string(policy) +
                                        "\n"
                                        "    priority: 50\n"
                                        "    affinity: []\n"
                                        "non_ros_threads: []\n"))
        << policy;
  }
}

TEST_F(ThreadConfiguratorNodeYamlTest, IgnoresStrayKeyOfTheOtherPolicyClass) {
  EXPECT_NO_THROW(make_node_from_yaml(R"YAML(
callback_groups:
  - id: cfs_cbg
    policy: SCHED_OTHER
    nice: -5
    priority: 50
    affinity: []
  - id: rt_cbg
    policy: SCHED_FIFO
    priority: 50
    nice: 10
    affinity: []
non_ros_threads: []
)YAML"));
}

TEST_F(ThreadConfiguratorNodeYamlTest, RejectsMissingNiceOnSchedOther) {
  try {
    make_node_from_yaml(R"YAML(
callback_groups:
  - id: my_cbg
    policy: SCHED_OTHER
    affinity: []
non_ros_threads: []
)YAML");
    FAIL() << "expected std::runtime_error";
  } catch (const std::runtime_error &e) {
    EXPECT_NE(std::string(e.what()).find("requires 'nice'"), std::string::npos)
        << e.what();
  }
}

// The legacy key on a CFS entry must fail loudly instead of being read.
TEST_F(ThreadConfiguratorNodeYamlTest, RejectsLegacyPriorityKeyOnSchedOther) {
  EXPECT_THROW(make_node_from_yaml(R"YAML(
callback_groups:
  - id: my_cbg
    policy: SCHED_OTHER
    priority: 0
    affinity: []
non_ros_threads: []
)YAML"),
               std::runtime_error);
}

TEST_F(ThreadConfiguratorNodeYamlTest, RejectsNiceOutOfRange) {
  for (const char *bad_nice : {"-21", "20", "50"}) {
    EXPECT_THROW(make_node_from_yaml("callback_groups:\n"
                                     "  - id: my_cbg\n"
                                     "    policy: SCHED_OTHER\n"
                                     "    nice: " +
                                     std::string(bad_nice) +
                                     "\n"
                                     "    affinity: []\n"
                                     "non_ros_threads: []\n"),
                 std::runtime_error)
        << "nice=" << bad_nice;
  }
}

TEST_F(ThreadConfiguratorNodeYamlTest, TreatsNullNiceAsMissing) {
  try {
    make_node_from_yaml(R"YAML(
callback_groups:
  - id: my_cbg
    policy: SCHED_OTHER
    nice:
    affinity: []
non_ros_threads: []
)YAML");
    FAIL() << "expected std::runtime_error";
  } catch (const std::runtime_error &e) {
    EXPECT_NE(std::string(e.what()).find("requires 'nice'"), std::string::npos)
        << e.what();
  }
}

TEST_F(ThreadConfiguratorNodeYamlTest, ReportsEntryOnNonIntegerNice) {
  try {
    make_node_from_yaml(R"YAML(
callback_groups:
  - id: my_cbg
    policy: SCHED_OTHER
    nice: low
    affinity: []
non_ros_threads: []
)YAML");
    FAIL() << "expected std::runtime_error";
  } catch (const std::runtime_error &e) {
    const std::string what = e.what();
    EXPECT_NE(what.find("'nice' must be an integer"), std::string::npos)
        << what;
    EXPECT_NE(what.find("id=my_cbg"), std::string::npos) << what;
  }
}

TEST_F(ThreadConfiguratorNodeYamlTest, ReportsEntryOnNonIntegerRtPriority) {
  try {
    make_node_from_yaml(R"YAML(
callback_groups:
  - id: my_cbg
    policy: SCHED_FIFO
    priority: high
    affinity: []
non_ros_threads: []
)YAML");
    FAIL() << "expected std::runtime_error";
  } catch (const std::runtime_error &e) {
    const std::string what = e.what();
    EXPECT_NE(what.find("'priority' must be an integer"), std::string::npos)
        << what;
    EXPECT_NE(what.find("id=my_cbg"), std::string::npos) << what;
  }
}

TEST_F(ThreadConfiguratorNodeYamlTest, RejectsMissingPriorityOnRtPolicy) {
  try {
    make_node_from_yaml(R"YAML(
callback_groups:
  - id: my_cbg
    policy: SCHED_FIFO
    affinity: []
non_ros_threads: []
)YAML");
    FAIL() << "expected std::runtime_error";
  } catch (const std::runtime_error &e) {
    EXPECT_NE(std::string(e.what()).find("requires 'priority'"),
              std::string::npos)
        << e.what();
  }
}

TEST_F(ThreadConfiguratorNodeYamlTest, RejectsRtPriorityOutOfRange) {
  for (const char *bad_priority : {"0", "100", "-1"}) {
    EXPECT_THROW(make_node_from_yaml("callback_groups:\n"
                                     "  - id: my_cbg\n"
                                     "    policy: SCHED_FIFO\n"
                                     "    priority: " +
                                     std::string(bad_priority) +
                                     "\n"
                                     "    affinity: []\n"
                                     "non_ros_threads: []\n"),
                 std::runtime_error)
        << "priority=" << bad_priority;
  }
}

// non_ros_threads entries go through the same loader as callback_groups.
TEST_F(ThreadConfiguratorNodeYamlTest, ValidatesNonRosThreadEntriesToo) {
  EXPECT_NO_THROW(make_node_from_yaml(R"YAML(
callback_groups: []
non_ros_threads:
  - id: worker
    policy: SCHED_OTHER
    nice: 10
    affinity: []
)YAML"));

  EXPECT_THROW(make_node_from_yaml(R"YAML(
callback_groups: []
non_ros_threads:
  - id: worker
    policy: SCHED_OTHER
    priority: 10
    affinity: []
)YAML"),
               std::runtime_error);
}
