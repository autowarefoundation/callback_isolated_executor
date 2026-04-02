# callback_isolated_executor
The ComponentContainer and Executor that assign a dedicated thread for each callback group.
This repository continues development from [https://github.com/sykwer/callback_isolated_executor](https://github.com/sykwer/callback_isolated_executor), which provides the implementation for [this paper](https://arxiv.org/pdf/2505.06546).


If you find `CallbackIsolatedExecutor` is useful in your research, please consider citing:
- T. Ishikawa-Aso, A. Yano, T. Azumi, and S. Kato, “Work in Progress: Middleware-Transparent Callback Enforcement in Commoditized Component-Oriented Real-Time Systems,” in Proc. of 31st IEEE Real-Time and Embedded Technology and Applications Symposium (RTAS), 2025, pp. 426–429.

<details>
<summary>BibTeX</summary>

```bibtex
@inproceedings{ishikawa2025work,
  title={Work in Progress: Middleware-Transparent Callback Enforcement in Commoditized Component-Oriented Real-Time Systems},
  author={Ishikawa-Aso, Takahiro and Yano, Atsushi and Azumi, Takuya and Kato, Shinpei},
  booktitle={Proc. of 31st IEEE Real-Time and Embedded Technology and Applications Symposium (RTAS)},
  pages={426--429},
  year={2025},
  organization={IEEE}
}
```
</details>

For a comparison of real-time performance with other ROS 2 executors, see [Comparison of Real-Time Performance with Other ROS 2 Executors](docs/comparison_with_other_executors.md).

## Supported Environments

CallbackIsolatedExecutor is currently available in the following environments.
This reflects the current status, and support is expected to expand in the future.

| Category           | Supported Versions / Notes                 |
| ------------------ | ------------------------------------------ |
| ROS 2              | Humble (only with `rclcpp` client library) |
| Linux Distribution | Ubuntu 22.04 (Jammy Jellyfish)             |

## Build and Install
```
$ git clone https://github.com/autowarefoundation/callback_isolated_executor.git
$ cd callback_isolated_executor
$ source /opt/ros/humble/setup.bash
$ colcon build --symlink-install --cmake-args -DCMAKE_BUILD_TYPE=Release
$ source install/setup.bash
```

Set capability for the configurator executable to issue the syscalls like `sched_setscheduler(2)`.
```
$ sudo setcap cap_sys_nice+ep ./build/cie_thread_configurator/thread_configurator_node
```

After elevating the priviridge level, part of dynamic linking functionality gets disabled for the security reason.
To deal with it, add a file with the following content under the `/etc/ld.so.conf.d/` directory.
The file name has to be `*.conf`.

```
/opt/ros/humble/lib
/opt/ros/humble/lib/x86_64-linux-gnu
/path/to/callback_isolated_executor/install/cie_config_msgs/lib
```

To enable the configuration, type the command below.
```
$ sudo ldconfig
```

<details>
<summary>Why ldconfig changed?</summary>


When specific permissions are granted to an ELF binary using setcap, for security reasons, environment variables like `LD_PRELOAD` and `LD_LIBRARY_PATH` are ignored.
While setting `RUNPATH` on the binary comes to mind as a solution, `RUNPATH` does not easily handle recursive dynamic linking.
In such cases, modifying `/etc/ld.so.conf.d/` is the only option.
</details>

## Kernel Boot Parameter
According to [the Linux Kernel documentation](https://www.kernel.org/doc/Documentation/scheduler/sched-deadline.txt), setting the affinity for `SCHED_DEADLINE` tasks requires the use of cgroup v1 features.
To use cgroup v1, it is necessary to disable cgroup v2 by specifying `systemd.unified_cgroup_hierarchy=0` in the kernel boot parameters.

To change the kernel boot parameters, edit `/etc/default/grub` and add the parameter to `GRUB_CMDLINE_LINUX_DEFAULT`:
```
GRUB_CMDLINE_LINUX_DEFAULT="... systemd.unified_cgroup_hierarchy=0 ..."
```

To apply these changes, run the following commands. After rebooting, the features of cgroup v1 will be available:
```bash
$ sudo update-grub
$ sudo reboot
```

## Usage
### Step1: Rewrite your app
When running a node within `ComponentContainerCallbackIsolated`, you don't need to modify the node's implementation.
However, if starting the node directly from the main function without using ComponentContainer, you need to modify the node's implementation as shown below and rebuild it.
Refer to the source code in the [cie_sample_application](https://github.com/autowarefoundation/callback_isolated_executor/tree/main/cie_sample_application) package to understand how to modify your app.

#### Option1: Launch without ComponentContainer
If you are launching a node directly from the main function without using a ComponentContainer, change the name of the Executor.
```xml
<?xml version="1.0"?>
<?xml-model href="http://download.ros.org/schema/package_format3.xsd" schematypens="http://www.w3.org/2001/XMLSchema"?>
<package format="3">
  ...
  <depend>callback_isolated_executor</depend>
  ...
</package>
```
```cmake
...
find_package(callback_isolated_executor REQUIRED)
...
ament_target_dependencies(your_executable ... callback_isolated_executor)
...
```
```cpp
#include "callback_isolated_executor.hpp"

int main(int argc, char * argv[]) {
  rclcpp::init(argc, argv);

  auto node = std::make_shared<SampleNode>();
  auto executor = std::make_shared<CallbackIsolatedExecutor>();

  executor->add_node(node);
  executor->spin();

  rclcpp::shutdown();
  return 0;
}
```
#### Option2: Launch with ComponentContainer
If you are launching a node within `ComponentContainerCallbackIsolated`, all you have to do is modify the launch file as below.
```xml
<launch>
  <node_container pkg="callback_isolated_executor" exec="component_container_callback_isolated" name="sample_container" namespace="">
    <composable_node pkg="cie_sample_application" plugin="SampleNode" name="sample_node" namespace="">
      ...
    </composable_node>
  </node_container>
</launch>
```

Or, you can load the node to the existing component container.
```xml
<launch>
    <load_composable_node target="sample_container">
        <composable_node pkg="cie_sample_application" plugin="SampleNode" name="sample_node" namespace="">
        </composable_node>
    </load_composable_node>
</launch>
```

### Step2: Rebuild your app
If you modify the application's source code, a rebuild is necessary.

### Step3: Generate yaml template file
Open two terminal windows.
In one, launch a ROS 2 node to create a template for the YAML configuration file (`prerun` node), and in the other, launch the target ROS 2 application.
After launching the `prerun` node, launch your ROS 2 application.

To launch the `prerun` node, type the command below.
```bash
$ ros2 run cie_thread_configurator thread_configurator_node --prerun
```

Then launch your ROS 2 application in another terminal window, after which you can see log messages like shown below in the `prerun` node window.
Each entry corresponds to the callback group ID and its OS thread ID.

<img width="1198" height="201" alt="cie_image0" src="https://github.com/user-attachments/assets/b50374e8-f8c9-4533-bb7e-1343e4031844" />


Once all the nodes of the target application are up and the logs in the prerun node window have stopped, press Control+C in the prerun node window.
Then, in the current directory, a template for the YAML configuration file `template.yaml` will be created in the format like below.

```yaml
hardware_info:
  model_name: Intel(R) Xeon(R) Silver 4216 CPU @ 2.10GHz
  cpu_family: 5
  model: 85
  threads_per_core: 2
  frequency_boost: enabled
  cpu_max_mhz: 2101.0000
  cpu_min_mhz: 800.0000

callback_groups:
  - id: /sample_node@Subscription(/parameter_events)@Service(/sample_node/get_parameters)@Service(/sample_node/get_parameter_types)@Service(/sample_node/set_parameters)@Service(/sample_node/set_parameters_atomically)@Service(/sample_node/describe_parameters)@Service(/sample_node/list_parameters)
    affinity: ~
    policy: SCHED_OTHER
    priority: 0

  - id: /sample_node@Subscription(/topic_in)
    affinity: ~
    policy: SCHED_OTHER
    priority: 0

  - id: /sample_node@Timer(1333000000)
    affinity: ~
    policy: SCHED_OTHER
    priority: 0

  - id: /sample_node@Timer(3000000000)
    affinity: ~
    policy: SCHED_OTHER
    priority: 0
```

Once the creation of template.yaml is complete, please also terminate the target ROS 2 application.

The `template.yaml` file includes captured hardware information from the system (CPU details retrieved via `lscpu`). This information will be used for validation when loading the configuration file to ensure compatibility with the target system.

### Step4: Edit yaml file for scheduler configuration
Change the file name and edit to configure each callback group.
```bash
$ mv template.yaml your_config.yaml
$ vim your_config.yaml
```

For callback groups that do not require configuration, you can either delete the entry entirely or leave it as is because the default values in `template.yaml` are set with default nice values and no affinity settings on the CFS scheduler.
For the detailed specifications of the configuration file, please refer to https://github.com/autowarefoundation/callback_isolated_executor/tree/main/cie_thread_configurator#yaml-configuration-file-format.

### Step5: Launch your app with scheduler configuration
To launch the target ROS 2 application with the scheduler settings applied from the your_config.yaml you created, first start the configurator node with the following command.

```bash
$ ros2 run cie_thread_configurator thread_configurator_node --config-file your_config.yaml
```

If there is a callback group with the `SCHED_DEADLINE` scheduling policy specified, running the configurator node requires root privileges.
This is because it is not possible to set threads to SCHED_DEADLINE within the permissions that can be granted through capability.
Note that if the target ROS 2 application is operating with a specific ROS_DOMAIN_ID, the configurator node must also be operated with the same ROS_DOMAIN_ID.

```bash
$ sudo bash -c "export ROS_DOMAIN_ID=[app domain id]; source /path/to/callback_isolated_executor/install/setup.bash; ros2 run cie_thread_configurator thread_configurator_node --config-file your_config.yaml"
```

Immediately after launching the configurator node, it will validate the hardware configuration. The configurator compares the hardware information stored in the configuration file against the current system's hardware details. If there are any mismatches (such as different CPU family or model), the configurator will report an error like:
```
[ERROR] Hardware validation failed with the following mismatches:
  - CPU family: expected '5', got '6'
```
This validation ensures that the scheduler configuration is applied only on compatible hardware to prevent potential performance or stability issues.

After successful validation, the configurator will print the settings and then wait for the target ROS 2 application to start running as follows.

<img width="1292" height="366" alt="cie_image1" src="https://github.com/user-attachments/assets/537034e0-167d-40dd-83ad-72c6efba7af8" />

In this state, when you launch the target ROS 2 application, the configurator node will receive callback group information from the application.
The entries in the configurator window show the callback group ID and OS thread ID information received from the ROS 2 application.

Settings with policies other than `SCHED_DEADLINE` are applied immediately as each callback group is received.
Once all callback groups have been registered, `SCHED_DEADLINE` settings are automatically applied together.

<details>
<summary>Why delayed configuration of the SCHED_DEADLINE policy?</summary>

We delay the applying of settings with the `SCHED_DEADLINE` policy because Autoware (main application area for this tool) contains nodes that implicitly create new threads immediately after startup, such as the EKF Localizer.
Threads specified with the `SCHED_DEADLINE` policy are prohibited from creating new child tasks.
Generally, real-time scheduling policies fail with an `EAGAIN` error when `clone(2)` is issued without the `SCHED_FLAG_RESET_ON_FORK` flag set.
However, setting this flag for `SCHED_DEADLINE` threads is impossible due to the following facts:
- According to [the Linux documentation for sched_setattr(2)](https://man7.org/linux/man-pages/man2/sched_setattr.2.html), the `flags` argument is currently required to be set to `0`, indicating that `SCHED_FLAG_RESET_ON_FORK` can only be set via `sched_setscheduler(2)`.
- According to [the Linux documentation for sched_setscheduler(2)](https://man7.org/linux/man-pages/man2/sched_setscheduler.2.html), the `SCHED_DEADLINE` policy can only be set via `sched_setattr(2)` and not through `sched_setscheduler(2)`.

Thus, we adopt a workaround where we delay only the settings that include the `SCHED_DEADLINE` policy.
Autoware's EKF Localizer creates child threads immediately after starting and does not create more afterwards (these child threads are likely deleted soon after).
Therefore, applying the `SCHED_DEADLINE` policy after the system has started, such as after starting playback of a rosbag or the operation of a real vehicle system, will not cause issues.
You need to apply the `SCHED_DEADLINE` policy only after the system has fully started up and no further creation of new child threads is expected.
</details>

The configurator node runs persistently and will automatically re-apply configurations if the target ROS 2 application is restarted.
To terminate the configurator node, press Ctrl+C.
If `SCHED_DEADLINE` policies were used, the configurator node will attempt to remove the cpuset cgroups it created for affinity settings when it exits. This cleanup will only succeed if the target application has already stopped and no threads remain attached to those cgroups; otherwise, you may need to stop the application first and/or remove the remaining cgroups manually.

## Notes on Adoption

When replacing `rclcpp::executors::SingleThreadedExecutor` with `CallbackIsolatedExecutor`, a dedicated thread is created for each CallbackGroup, enabling parallel execution.
This may expose concurrency bugs that were previously hidden. Therefore, before adoption, you should carefully investigate whether there are any locations that require mutexes or synchronization.
Note that callbacks within the same `MutuallyExclusiveCallbackGroup` are still executed serially.

When using real-time scheduling policies, such as `SCHED_FIFO` or `SCHED_DEADLINE`, you must carefully consider the potential for delaying kernel processing and other applications, and configure affinity, priority, and [throttling](https://man7.org/linux/man-pages/man7/sched.7.html) appropriately.
