# Comparison of Real-Time Performance with Other ROS 2 Executors

This document compares the real-time performance of the ROS 2 Default Executor and Executors proposed in academic literature from the following perspectives:

**Immediate Enqueue of Ready Callbacks**

In the ROS 2 Default Executor, a callback instance that becomes ready is enqueued only when the queue becomes empty. If the callback is enqueued immediately at the moment it becomes ready, ✔ is marked.

**Per-Callback Priority Assignment**

The ROS 2 Default Executor does not provide an API to assign priorities on a per-callback basis. If an Executor provides such an API, or allows users to assign priorities per callback through another mechanism, ✔ is marked.

**Non-Nested Scheduling**

In many Executors, the Executor maintains its own scheduling layer (e.g., a ready queue with priority ordering), and the OS independently schedules the threads that run callbacks. This two-level structure is referred to as nested scheduling.
If the Executor's scheduling and the OS scheduling do not form such a nested structure, ✔ is marked.

**Fully Preemptive**

If a lower-priority callback can be preempted by any higher-priority callback during execution, ✔ is marked.

**Support for Reentrant Callbacks**

If, when a callback becomes ready while it is executing, it can be properly enqueued and executed, ✔ is marked.

**ROS 2 Mainline**

If the Executor has been merged into the ROS 2 mainline, or can be used by simply installing a standalone package without requiring changes to rcl or rclcpp, ✔ is marked.

---

## Comparison Table

| Executor                                       | Immediate Enqueue | Per-Callback Priority | Non-Nested Scheduling | Fully Preemptive | Support for Reentrant Callbacks | ROS 2 Mainline |
| ---------------------------------------------- | :---------------: | :-------------------: | :-------------------: | :--------------: | :-----------------------------: | :------------: |
| SingleThreadedExecutor / MultiThreadedExecutor |                   |                       |                       |                  |                                | ✔              |
| EventExecutor                                  | ✔                 |                       |                       |                  | ✔                              | ✔              |
| PiCAS Single [1]                               |                   | ✔                     |                       |                  |                                |                |
| PiCAS Multi [2]                                | ✔\*1              | ✔                     |                       |                  |                                |                |
| Dynamic-Priority-based Executor [3,4]          | ✔\*1              | ✔                     |                       |                  |                                |                |
| Extended EventExecutor [5]                     | ✔                 | ✔                     |                       |                  | ✔                              |                |
| RTeX [6]                                       | ✔                 | ✔                     |                       |                  | ✔                              |                |
| Budget-based Real-Time Executor [7]            | ✔\*1              | ✔                     | ✔                     | ✔                |                                |                |
| Preemptive EDF Executor [8]                    | ✔\*1              | ✔                     | ✔                     | ✔                |                                |                |
| Ros-RT [9]                                     | ✔                 | ✔                     | ✔                     | ✔                | ✔                              |                |
| CallbackIsolatedExecutor [10]                  | ✔\*1              | ✔                     | ✔                     | ✔                | ✔                              | ✔              |

\*1: The ready queue is updated each time a thread selects a callback to execute.

---

## References

1. H. Choi, Y. Xiang and H. Kim,
   “PiCAS: New Design of Priority-Driven Chain-Aware Scheduling for ROS2,”
   *2021 IEEE 27th Real-Time and Embedded Technology and Applications Symposium (RTAS)*, Nashville, TN, USA, 2021, pp. 251–263.
   doi: 10.1109/RTAS52030.2021.00028

2. H. Sobhani, H. Choi and H. Kim,
   “Timing Analysis and Priority-driven Enhancements of ROS 2 Multi-threaded Executors,”
   *2023 IEEE 29th RTAS*, San Antonio, TX, USA, 2023, pp. 106–118.
   doi: 10.1109/RTAS58335.2023.00016

3. A. Al Arafat et al.,
   “Response time analysis for dynamic priority scheduling in ROS2,”
   *DAC ’22*, ACM, 2022, pp. 301–306.
   doi: 10.1145/3489517.3530447

4. A. Al Arafat et al.,
   “Dynamic Priority Scheduling of Multithreaded ROS 2 Executor With Shared Resources,”
   *IEEE Transactions on CAD*, vol. 43, no. 11, pp. 3732–3743, Nov. 2024.
   doi: 10.1109/TCAD.2024.3445259

5. H. Teper et al.,
   “Reconciling ROS 2 with Classical Real-Time Scheduling of Periodic Tasks,”
   *2025 IEEE 31st RTAS*, Irvine, CA, USA, 2025, pp. 177–189.
   doi: 10.1109/RTAS65571.2025.00027

6. S. Liu et al.,
   “RTeX: An Efficient and Timing-Predictable Multithreaded Executor for ROS 2,”
   *IEEE Transactions on CAD*, vol. 43, no. 9, pp. 2578–2591, Sept. 2024.
   doi: 10.1109/TCAD.2024.3380551

7. J. Staschulat et al.,
   “Budget-based real-time Executor for Micro-ROS,”
   arXiv:2105.05590, 2021.

8. K. Wilson et al.,
   “Physics-Informed Mixed-Criticality Scheduling for F1Tenth Cars with Preemptable ROS 2 Executors,”
   *2025 IEEE 31st RTAS*, Irvine, CA, USA, 2025, pp. 215–227.
   doi: 10.1109/RTAS65571.2025.00030

9. S. Liu et al.,
   “Ros-RT: Enabling Flexible Scheduling in ROS 2,”
   *2025 IEEE Real-Time Systems Symposium (RTSS)*, Dec. 2025, pp. 42–54.
   doi: 10.1109/RTSS66672.2025.00013

10. T. Ishikawa-Aso et al.,
    “Middleware-Transparent Callback Enforcement in Commoditized Component-Oriented Real-Time Systems,”
    *2025 IEEE 31st RTAS*, Irvine, CA, USA, 2025, pp. 426–429.
    doi: 10.1109/RTAS65571.2025.00017
