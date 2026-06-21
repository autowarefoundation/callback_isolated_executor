import unittest

import launch
import launch_ros.actions
import launch_testing.actions
import launch_testing.asserts
import launch_testing.markers
import pytest
import rclpy
from rclpy.node import Node
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

    def test_echo_messages_flow(self):
        received = []
        self.node.create_subscription(
            Int32, "echo", lambda msg: received.append(msg.data), 10
        )

        end_time = self.node.get_clock().now().nanoseconds + 15 * 1_000_000_000
        target_count = 5
        while (
            len(received) < target_count
            and self.node.get_clock().now().nanoseconds < end_time
        ):
            rclpy.spin_once(self.node, timeout_sec=0.1)

        self.assertGreaterEqual(
            len(received),
            target_count,
            f"Expected >= {target_count} echo messages, got {len(received)}: {received}",
        )
        # Values come from an incrementing counter; the observed subsequence must
        # be strictly increasing. We do not require start==0 because discovery
        # latency may drop the first few messages.
        for earlier, later in zip(received, received[1:]):
            self.assertLess(earlier, later)


@launch_testing.post_shutdown_test()
class TestCiePubSubShutdown(unittest.TestCase):
    def test_exit_code(self, proc_info, cie_pubsub_node):
        # 0 = clean return after spin(); -2 = terminated by SIGINT (distro-dependent).
        launch_testing.asserts.assertExitCodes(
            proc_info, allowable_exit_codes=[0, -2], process=cie_pubsub_node
        )
