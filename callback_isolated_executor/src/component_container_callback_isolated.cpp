#include <chrono>
#include <list>
#include <sys/syscall.h>
#include <thread>
#include <unordered_map>

#include "rclcpp/rclcpp.hpp"
#include "rclcpp_components/component_manager.hpp"

#include "callback_isolated_executor/multi_threaded_executor_internal.hpp"
#include "cie_thread_configurator/cie_thread_configurator.hpp"

namespace rclcpp_components {

class ComponentManagerCallbackIsolated
    : public rclcpp_components::ComponentManager {

  struct ExecutorWrapper {
    explicit ExecutorWrapper(std::shared_ptr<rclcpp::Executor> executor)
        : executor(executor), thread_initialized(false) {}

    ExecutorWrapper(const ExecutorWrapper &) = delete;
    ExecutorWrapper &operator=(const ExecutorWrapper &) = delete;

    std::shared_ptr<rclcpp::Executor> executor;
    std::thread thread;
    std::atomic_bool thread_initialized;
  };

public:
  template <typename... Args>
  ComponentManagerCallbackIsolated(Args &&...args)
      : rclcpp_components::ComponentManager(std::forward<Args>(args)...) {
    client_publisher_ =
        create_publisher<cie_config_msgs::msg::CallbackGroupInfo>(
            "/cie_thread_configurator/callback_group_info",
            rclcpp::QoS(1000).keep_all());

    // Declare and get parameters for MultiThreadedExecutorInternal
    auto reentrant_parallelism_param =
        declare_parameter("reentrant_parallelism", 4);
    if (reentrant_parallelism_param < 0) {
      RCLCPP_WARN(get_logger(),
                  "reentrant_parallelism must be non-negative, using default "
                  "value 4");
      reentrant_parallelism_param = 4;
    }
    reentrant_parallelism_ = static_cast<size_t>(reentrant_parallelism_param);
    yield_before_execute_ = declare_parameter("yield_before_execute", false);
    auto timeout_ns = declare_parameter("next_exec_timeout_ns", -1L);
    next_exec_timeout_ = std::chrono::nanoseconds(timeout_ns);
  }

  ~ComponentManagerCallbackIsolated();

protected:
  void add_node_to_executor(uint64_t node_id) override;
  void remove_node_from_executor(uint64_t node_id) override;

private:
  void cancel_executor(ExecutorWrapper &executor_wrapper);
  bool is_clock_callback_group(rclcpp::CallbackGroup::SharedPtr group);

  std::unordered_map<uint64_t, std::list<ExecutorWrapper>>
      node_id_to_executor_wrappers_;
  rclcpp::Publisher<cie_config_msgs::msg::CallbackGroupInfo>::SharedPtr
      client_publisher_;
  std::mutex client_publisher_mutex_;

  // Parameters for the MultiThreadedExecutorInternal used for reentrant
  // callback groups
  size_t reentrant_parallelism_{4};
  bool yield_before_execute_{false};
  std::chrono::nanoseconds next_exec_timeout_{std::chrono::nanoseconds(-1)};
};

ComponentManagerCallbackIsolated::~ComponentManagerCallbackIsolated() {
  if (node_wrappers_.size() == 0)
    return;

  for (auto &p : node_id_to_executor_wrappers_) {
    auto &executor_wrappers = p.second;

    for (auto &executor_wrapper : executor_wrappers) {
      cancel_executor(executor_wrapper);
    }
  }

  node_wrappers_.clear();
}

bool ComponentManagerCallbackIsolated::is_clock_callback_group(
    rclcpp::CallbackGroup::SharedPtr group) {
  int sub_num = 0;
  int service_num = 0;
  int client_num = 0;
  int timer_num = 0;

  bool clock_sub_exists = false;

  auto sub_func = [&](const rclcpp::SubscriptionBase::SharedPtr &sub) {
    sub_num++;
    if (strcmp(sub->get_topic_name(), "/clock") == 0)
      clock_sub_exists = true;
  };

  auto service_func = [&](const rclcpp::ServiceBase::SharedPtr &service) {
    (void)service;
    service_num++;
  };

  auto client_func = [&](const rclcpp::ClientBase::SharedPtr &client) {
    (void)client;
    client_num++;
  };

  auto timer_func = [&](const rclcpp::TimerBase::SharedPtr &timer) {
    (void)timer;
    timer_num++;
  };

  auto waitable_func = [](const rclcpp::Waitable::SharedPtr &waitable) {
    (void)waitable;
  };

  group->collect_all_ptrs(sub_func, service_func, client_func, timer_func,
                          waitable_func);

  return sub_num == 1 && clock_sub_exists && service_num == 0 &&
         client_num == 0 && timer_num == 0;
}

void ComponentManagerCallbackIsolated::add_node_to_executor(uint64_t node_id) {
  auto node = node_wrappers_[node_id].get_node_base_interface();

  node->for_each_callback_group([node_id, &node,
                                 this](rclcpp::CallbackGroup::SharedPtr
                                           callback_group) {
    if (!callback_group->automatically_add_to_executor_with_node()) {
      return;
    }

    std::string group_id =
        cie_thread_configurator::create_callback_group_id(callback_group, node);
    std::atomic_bool &has_executor =
        callback_group->get_associated_with_executor_atomic();

    if (is_clock_callback_group(callback_group) /* workaround */ ||
        has_executor.load()) {
      RCLCPP_WARN(
          this->get_logger(),
          "A callback group (%s) has already been added to an executor. skip.",
          group_id.c_str());
      return;
    }

    if (callback_group->type() == rclcpp::CallbackGroupType::Reentrant &&
        reentrant_parallelism_ >= 2) {
      // Reentrant callback group: use MultiThreadedExecutorInternal
      auto reentrant_executor = std::make_shared<MultiThreadedExecutorInternal>(
          reentrant_parallelism_, yield_before_execute_, next_exec_timeout_);
      reentrant_executor->add_callback_group(callback_group, node);

      auto it = node_id_to_executor_wrappers_[node_id].begin();
      it = node_id_to_executor_wrappers_[node_id].emplace(it,
                                                          reentrant_executor);
      auto &executor_wrapper = *it;

      executor_wrapper.thread = std::thread(
          [&executor_wrapper, reentrant_executor, group_id, this]() {
            reentrant_executor->pre_spin();
            auto tids = reentrant_executor->get_thread_ids();

            {
              std::lock_guard<std::mutex> lock(this->client_publisher_mutex_);
              for (auto tid : tids) {
                cie_thread_configurator::publish_callback_group_info(
                    this->client_publisher_, tid, group_id);
              }
            }

            executor_wrapper.thread_initialized = true;
            executor_wrapper.executor->spin();
          });
    } else {
      // Mutually exclusive callback group: use SingleThreadedExecutor
      auto executor =
          std::make_shared<rclcpp::executors::SingleThreadedExecutor>();
      executor->add_callback_group(callback_group, node);

      auto it = node_id_to_executor_wrappers_[node_id].begin();
      it = node_id_to_executor_wrappers_[node_id].emplace(it, executor);
      auto &executor_wrapper = *it;

      executor_wrapper.thread =
          std::thread([&executor_wrapper, group_id, this]() {
            auto tid = syscall(SYS_gettid);

            {
              std::lock_guard<std::mutex> lock(this->client_publisher_mutex_);
              cie_thread_configurator::publish_callback_group_info(
                  this->client_publisher_, tid, group_id);
            }

            executor_wrapper.thread_initialized = true;
            executor_wrapper.executor->spin();
          });
    }
  });
}

void ComponentManagerCallbackIsolated::remove_node_from_executor(
    uint64_t node_id) {
  auto it = node_id_to_executor_wrappers_.find(node_id);
  if (it == node_id_to_executor_wrappers_.end())
    return;

  for (ExecutorWrapper &executor_wrapper : it->second) {
    cancel_executor(executor_wrapper);
  }

  node_id_to_executor_wrappers_.erase(it);
}

void ComponentManagerCallbackIsolated::cancel_executor(
    ExecutorWrapper &executor_wrapper) {
  if (!executor_wrapper.thread_initialized) {
    auto context = this->get_node_base_interface()->get_context();

    while (!executor_wrapper.executor->is_spinning() && rclcpp::ok(context)) {
      rclcpp::sleep_for(std::chrono::milliseconds(1));
    }
  }

  executor_wrapper.executor->cancel();
  executor_wrapper.thread.join();
}

} // namespace rclcpp_components

int main(int argc, char *argv[]) {
  rclcpp::init(argc, argv);

  auto executor = std::make_shared<rclcpp::executors::SingleThreadedExecutor>();
  auto node =
      std::make_shared<rclcpp_components::ComponentManagerCallbackIsolated>();

  executor->add_node(node);
  executor->spin();

  rclcpp::shutdown();
}
