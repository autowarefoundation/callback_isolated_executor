#pragma once

#include "rclcpp/rclcpp.hpp"

// To implement parallelism for reentrant callback groups in
// CallbackIsolatedExecutor
class MultiThreadedExecutorInternal : public rclcpp::Executor {
  RCLCPP_DISABLE_COPY(MultiThreadedExecutorInternal)

  // Configuration
  size_t number_of_threads_;
  bool yield_before_execute_;
  std::chrono::nanoseconds next_exec_timeout_;

  // Thread management
  std::vector<std::thread> threads_;
  std::vector<pid_t> tids_;   // guarded by mtx_
  size_t ready_count_{0};     // guarded by mtx_ (# of threads that saved TID)
  bool start_allowed_{false}; // guarded by mtx_ (run() allowed to proceed)

  // Synchronization in start phase
  std::mutex mtx_;
  std::condition_variable cv_all_ready_;
  std::condition_variable cv_start_;

  std::mutex wait_mutex_; // to guard get_next_executable in run()
  std::atomic_bool pre_spinning_{false};

  void run();

public:
  explicit MultiThreadedExecutorInternal(
      size_t number_of_threads, bool yield_before_execute = false,
      std::chrono::nanoseconds next_exec_timeout = std::chrono::nanoseconds(-1))
      : rclcpp::Executor(rclcpp::ExecutorOptions()),
        number_of_threads_(number_of_threads),
        yield_before_execute_(yield_before_execute),
        next_exec_timeout_(next_exec_timeout) {}

  void pre_spin();

  void spin() override;

  std::vector<pid_t> get_thread_ids();
};
