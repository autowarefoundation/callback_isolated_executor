import unittest

import launch
import launch_ros.actions
import launch_testing.actions
import launch_testing.asserts
import launch_testing.markers
import pytest
import rclpy
from cie_config_msgs.msg import CallbackGroupInfo
from rclpy.node import Node
from rclpy.qos import (
    QoSDurabilityPolicy,
    QoSHistoryPolicy,
    QoSProfile,
    QoSReliabilityPolicy,
)
from std_msgs.msg import Int32


@pytest.mark.launch_test
@launch_testing.markers.keep_alive
def generate_test_description():
    cie_pubsub_node = launch_ros.actions.Node(
        package="callback_isolated_executor",
        executable="cie_pubsub_test_node",
        name="cie_pubsub_test_node",
        output="screen",
    )
    return (
        launch.LaunchDescription(
            [cie_pubsub_node, launch_testing.actions.ReadyToTest()]
        ),
        {"cie_pubsub_node": cie_pubsub_node},
    )


class TestCiePubSub(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        rclpy.init()

    @classmethod
    def tearDownClass(cls):
        rclpy.shutdown()

    def setUp(self):
        self.node = Node("cie_pubsub_test_observer")

    def tearDown(self):
        self.node.destroy_node()

    def _spin_until(self, predicate, timeout_sec):
        end_time = self.node.get_clock().now().nanoseconds + int(
            timeout_sec * 1_000_000_000
        )
        while (
            not predicate()
            and self.node.get_clock().now().nanoseconds < end_time
        ):
            rclpy.spin_once(self.node, timeout_sec=0.1)

    def test_echo_messages_flow(self):
        received = []
        self.node.create_subscription(
            Int32, "echo", lambda msg: received.append(msg.data), 10
        )

        self._spin_until(lambda: len(received) >= 5, timeout_sec=15.0)

        self.assertGreaterEqual(
            len(received),
            5,
            f"Expected >= 5 echo messages, got {len(received)}: {received}",
        )
        # Values come from an incrementing counter; the observed subsequence must
        # be strictly increasing. We do not require start==0 because discovery
        # latency may drop the first few messages.
        for earlier, later in zip(received, received[1:]):
            self.assertLess(earlier, later)

    def test_callback_groups_isolated(self):
        # CIE publishes one (callback_group_id, thread_id) per callback group on a
        # transient_local topic, so a late-joining subscriber still receives them.
        # Distinct thread ids prove the callbacks are isolated onto separate threads.
        infos = []
        qos = QoSProfile(
            reliability=QoSReliabilityPolicy.RELIABLE,
            durability=QoSDurabilityPolicy.TRANSIENT_LOCAL,
            history=QoSHistoryPolicy.KEEP_ALL,
        )
        self.node.create_subscription(
            CallbackGroupInfo,
            "/cie_thread_configurator/callback_group_info",
            lambda msg: infos.append((msg.callback_group_id, msg.thread_id)),
            qos,
        )

        self._spin_until(lambda: len(infos) >= 2, timeout_sec=15.0)

        self.assertGreaterEqual(
            len(infos),
            2,
            f"Expected >= 2 callback group infos, got {infos}",
        )
        thread_ids = {thread_id for _, thread_id in infos}
        self.assertGreaterEqual(
            len(thread_ids),
            2,
            f"Expected callbacks isolated across >= 2 threads, got {infos}",
        )


@launch_testing.post_shutdown_test()
class TestCiePubSubShutdown(unittest.TestCase):
    def test_exit_code(self, proc_info, cie_pubsub_node):
        # 0 = clean return after spin(); -2 = terminated by SIGINT (distro-dependent).
        launch_testing.asserts.assertExitCodes(
            proc_info, allowable_exit_codes=[0, -2], process=cie_pubsub_node
        )
