#include <memory>
#include <string>
#include <sys/syscall.h>
#include <thread>
#include <vector>

#include "cie_thread_configurator/cie_thread_configurator.hpp"
#include "rclcpp/rclcpp.hpp"

#include "callback_isolated_executor/callback_isolated_executor.hpp"
#include "callback_isolated_executor/multi_threaded_executor_internal.hpp"

CallbackIsolatedExecutor::CallbackIsolatedExecutor(
    const rclcpp::ExecutorOptions &options, size_t reentrant_parallelism,
    bool yield_before_execute, std::chrono::nanoseconds next_exec_timeout)
    : rclcpp::Executor(options), reentrant_parallelism_(reentrant_parallelism),
      yield_before_execute_(yield_before_execute),
      next_exec_timeout_(next_exec_timeout) {
  client_publisher_ = cie_thread_configurator::create_client_publisher();
}

void CallbackIsolatedExecutor::spin() {
  std::vector<std::thread> threads;
  std::vector<std::pair<rclcpp::CallbackGroup::SharedPtr,
                        rclcpp::node_interfaces::NodeBaseInterface::SharedPtr>>
      groups_and_nodes;

  {
    std::lock_guard<std::mutex> guard{mutex_};

    for (const auto &weak_group_to_node : weak_groups_to_nodes_) {
      auto group = weak_group_to_node.first.lock();
      if (!group) {
        continue;
      }

      auto node = weak_group_to_node.second.lock();
      if (!node) {
        continue;
      }

      groups_and_nodes.emplace_back(group, node);
    }

    for (const auto &weak_node : weak_nodes_) {
      auto node = weak_node.lock();
      if (!node) {
        continue;
      }

      node->for_each_callback_group(
          [&groups_and_nodes,
           node](const rclcpp::CallbackGroup::SharedPtr &group) {
            if (group && group->automatically_add_to_executor_with_node()) {
              groups_and_nodes.emplace_back(group, node);
            }
          });
    }
  } // guard mutex_

  for (auto [group, node] : groups_and_nodes) {
    if (group->type() == rclcpp::CallbackGroupType::Reentrant &&
        reentrant_parallelism_ >= 2) {
      threads.emplace_back(
          &CallbackIsolatedExecutor::spin_reentrant_callback_group, this, group,
          node);
    } else {
      threads.emplace_back(
          &CallbackIsolatedExecutor::spin_mutually_exclusive_callback_group,
          this, group, node);
    }
  }

  for (auto &t : threads) {
    t.join();
  }
}

void CallbackIsolatedExecutor::spin_mutually_exclusive_callback_group(
    rclcpp::CallbackGroup::SharedPtr group,
    rclcpp::node_interfaces::NodeBaseInterface::SharedPtr node) {
  auto executor = std::make_shared<rclcpp::executors::SingleThreadedExecutor>();
  executor->add_callback_group(group, node);
  auto callback_group_id =
      cie_thread_configurator::create_callback_group_id(group, node);
  auto tid = static_cast<pid_t>(syscall(SYS_gettid));

  {
    std::lock_guard<std::mutex> lock{client_publisher_mutex_};
    cie_thread_configurator::publish_callback_group_info(client_publisher_, tid,
                                                         callback_group_id);
  }

  executor->spin();
}

void CallbackIsolatedExecutor::spin_reentrant_callback_group(
    rclcpp::CallbackGroup::SharedPtr group,
    rclcpp::node_interfaces::NodeBaseInterface::SharedPtr node) {
  auto executor = std::make_shared<MultiThreadedExecutorInternal>(
      reentrant_parallelism_, yield_before_execute_, next_exec_timeout_);
  executor->add_callback_group(group, node);
  auto callback_group_id =
      cie_thread_configurator::create_callback_group_id(group, node);

  executor->pre_spin();
  auto tids = executor->get_thread_ids();

  {
    std::lock_guard<std::mutex> lock{client_publisher_mutex_};
    for (auto tid : tids) {
      cie_thread_configurator::publish_callback_group_info(
          client_publisher_, tid, callback_group_id);
    }
  }

  executor->spin();
}

void CallbackIsolatedExecutor::add_callback_group(
    rclcpp::CallbackGroup::SharedPtr group_ptr,
    rclcpp::node_interfaces::NodeBaseInterface::SharedPtr node_ptr,
    bool notify) {
  (void)notify;

  std::weak_ptr<rclcpp::CallbackGroup> weak_group_ptr = group_ptr;
  std::weak_ptr<rclcpp::node_interfaces::NodeBaseInterface> weak_node_ptr =
      node_ptr;

  std::lock_guard<std::mutex> guard{mutex_};

  // Confirm that group_ptr does not refer to any of the callback groups held by
  // nodes in weak_nodes_.
  for (const auto &weak_node : weak_nodes_) {
    auto n = weak_node.lock();

    if (!n) {
      continue;
    }

    if (n->callback_group_in_node(group_ptr)) {
      RCLCPP_ERROR(rclcpp::get_logger("callback_isolated_executor"),
                   "Callback group already exists in node: %s",
                   n->get_fully_qualified_name());
      exit(EXIT_FAILURE);
    }
  }

  auto insert_info = weak_groups_to_nodes_.insert(
      std::make_pair(weak_group_ptr, weak_node_ptr));

  if (!insert_info.second) {
    RCLCPP_ERROR(rclcpp::get_logger("callback_isolated_executor"),
                 "Callback group already exists in the executor");
    exit(EXIT_FAILURE);
  }
}

std::vector<rclcpp::CallbackGroup::WeakPtr>
CallbackIsolatedExecutor::get_all_callback_groups() {
  std::lock_guard<std::mutex> guard{mutex_};
  std::vector<rclcpp::CallbackGroup::WeakPtr> groups;

  auto manually_added_groups = get_manually_added_callback_groups_internal();
  auto automatically_added_groups =
      get_automatically_added_callback_groups_from_nodes_internal();

  groups.reserve(manually_added_groups.size() +
                 automatically_added_groups.size());
  groups.insert(groups.end(), manually_added_groups.begin(),
                manually_added_groups.end());
  groups.insert(groups.end(), automatically_added_groups.begin(),
                automatically_added_groups.end());

  return groups;
}

std::vector<rclcpp::CallbackGroup::WeakPtr>
CallbackIsolatedExecutor::get_manually_added_callback_groups() {
  std::lock_guard<std::mutex> guard{mutex_};
  return get_manually_added_callback_groups_internal();
}

std::vector<rclcpp::CallbackGroup::WeakPtr>
CallbackIsolatedExecutor::get_manually_added_callback_groups_internal() const {
  std::vector<rclcpp::CallbackGroup::WeakPtr> groups;

  for (const auto &weak_group_to_node : weak_groups_to_nodes_) {
    auto group = weak_group_to_node.first.lock();
    if (group) {
      groups.push_back(group);
    }
  }

  return groups;
}

std::vector<rclcpp::CallbackGroup::WeakPtr>
CallbackIsolatedExecutor::get_automatically_added_callback_groups_from_nodes() {
  std::lock_guard<std::mutex> guard{mutex_};
  return get_automatically_added_callback_groups_from_nodes_internal();
}

std::vector<rclcpp::CallbackGroup::WeakPtr> CallbackIsolatedExecutor::
    get_automatically_added_callback_groups_from_nodes_internal() const {
  std::vector<rclcpp::CallbackGroup::WeakPtr> groups;

  for (const auto &weak_node : weak_nodes_) {
    auto node = weak_node.lock();

    if (!node) {
      continue;
    }

    node->for_each_callback_group(
        [&groups](const rclcpp::CallbackGroup::SharedPtr &group) {
          if (group && group->automatically_add_to_executor_with_node()) {
            groups.push_back(group);
          }
        });
  }

  return groups;
}

void CallbackIsolatedExecutor::remove_callback_group(
    rclcpp::CallbackGroup::SharedPtr group_ptr, bool notify) {
  (void)notify;

  std::lock_guard<std::mutex> guard{mutex_};

  auto it = weak_groups_to_nodes_.find(group_ptr);

  if (it != weak_groups_to_nodes_.end()) {
    weak_groups_to_nodes_.erase(it);
  } else {
    RCLCPP_ERROR(rclcpp::get_logger("callback_isolated_executor"),
                 "Callback group not found in the executor");
    exit(EXIT_FAILURE);
  }
}

void CallbackIsolatedExecutor::add_node(
    rclcpp::node_interfaces::NodeBaseInterface::SharedPtr node_ptr,
    bool notify) {
  (void)notify;

  std::lock_guard<std::mutex> guard{mutex_};

  // Confirm that any callback group in weak_groups_to_nodes_ does not refer to
  // any of the callback groups held by node_ptr.
  for (const auto &weak_group_to_node : weak_groups_to_nodes_) {
    auto group = weak_group_to_node.first.lock();

    if (!group) {
      continue;
    }

    if (node_ptr->callback_group_in_node(group)) {
      RCLCPP_ERROR(rclcpp::get_logger("callback_isolated_executor"),
                   "One of the callback groups in node %s already exists in "
                   "the executor.",
                   node_ptr->get_fully_qualified_name());
      exit(EXIT_FAILURE);
    }
  }

  std::weak_ptr<rclcpp::node_interfaces::NodeBaseInterface> weak_node_ptr =
      node_ptr;

  auto insert_info = weak_nodes_.insert(weak_node_ptr);
  if (!insert_info.second) {
    RCLCPP_ERROR(rclcpp::get_logger("callback_isolated_executor"),
                 "Node already exists in the executor: %s",
                 node_ptr->get_fully_qualified_name());
    exit(EXIT_FAILURE);
  }
}

void CallbackIsolatedExecutor::add_node(rclcpp::Node::SharedPtr node_ptr,
                                        bool notify) {
  (void)notify;
  add_node(node_ptr->get_node_base_interface(), notify);
}

void CallbackIsolatedExecutor::remove_node(
    rclcpp::node_interfaces::NodeBaseInterface::SharedPtr node_ptr,
    bool notify) {
  (void)notify;

  std::weak_ptr<rclcpp::node_interfaces::NodeBaseInterface> weak_node_ptr =
      node_ptr;
  std::lock_guard<std::mutex> guard{mutex_};

  auto it = weak_nodes_.find(weak_node_ptr);

  if (it != weak_nodes_.end()) {
    weak_nodes_.erase(it);
  } else {
    RCLCPP_ERROR(rclcpp::get_logger("callback_isolated_executor"),
                 "Node not found in the executor: %s",
                 node_ptr->get_fully_qualified_name());
    exit(EXIT_FAILURE);
  }
}

void CallbackIsolatedExecutor::remove_node(rclcpp::Node::SharedPtr node_ptr,
                                           bool notify) {
  remove_node(node_ptr->get_node_base_interface(), notify);
}
