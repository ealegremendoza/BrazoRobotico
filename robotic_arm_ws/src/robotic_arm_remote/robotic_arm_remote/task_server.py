#!/usr/bin/env python3
import rclpy
import numpy as np
from rclpy.node import Node
from rclpy.action import ActionServer
from robotic_arm_msgs.action import RoboticArmTask
from moveit.planning import MoveItPy
from moveit.core.robot_state import RobotState


class TaskServer(Node):
    def __init__(self):
        super().__init__("task_server")
        self.get_logger().info("Starting the Server")
        self.action_server = ActionServer(
            self, RoboticArmTask, "task_server", self.goalCallback
        )

        # MoveIt 2 Interface
        self.robotic_arm = MoveItPy(node_name="moveit_py")
        self.robotic_arm_arm = self.robotic_arm.get_planning_component("arm")
        self.robotic_arm_gripper = self.robotic_arm.get_planning_component("gripper")

    def goalCallback(self, goal_handle):
        self.get_logger().info(
            "Received goal request with id %d" % goal_handle.request.task_number
        )

        arm_state = RobotState(self.robotic_arm.get_robot_model())
        gripper_state = RobotState(self.robotic_arm.get_robot_model())

        arm_joint_goal = []
        gripper_joint_goal = []

        if goal_handle.request.task_number == 0:
            arm_joint_goal = np.array([0.0, 0.0, 0.0, 0.0, 0.0]) #zero
            gripper_joint_goal = np.array([0.0])
        elif goal_handle.request.task_number == 1: # posicion inicial
            arm_joint_goal = np.array([0.0, -1.689, 1.434, -1.569, -1.416])
            gripper_joint_goal = np.array([0.427])
        elif goal_handle.request.task_number == 2: # roto en reposo a posicion a y abro grip
            arm_joint_goal = np.array([1.360, -1.689, 1.434, -1.569, -1.416]) 
            gripper_joint_goal = np.array([0.427])
        elif goal_handle.request.task_number == 3: # agarro objeto
            arm_joint_goal = np.array([1.360, -0.802, 1.343, -0.654, -1.416]) 
            gripper_joint_goal = np.array([0.147])
        elif goal_handle.request.task_number == 4: # levanto y traslado a posicion centro agarrando objeto
            arm_joint_goal = np.array([0.0, -0.802, 1.032, -0.314, -1.416]) 
            gripper_joint_goal = np.array([0.147])
        elif goal_handle.request.task_number == 5: # roto con objeto agarrado a posicion b
            arm_joint_goal = np.array([-1.360, -0.802, 1.343, -0.654, -1.416]) 
            gripper_joint_goal = np.array([0.147])
        elif goal_handle.request.task_number == 6: # suelto objeto en posicion b
            arm_joint_goal = np.array([-1.360, -0.802, 1.343, -0.654, -1.416]) 
            gripper_joint_goal = np.array([0.427])
        else:
            self.get_logger().error("Invalid Task Number")
            return

        arm_state.set_joint_group_positions("arm", arm_joint_goal)
        gripper_state.set_joint_group_positions("gripper", gripper_joint_goal)

        self.robotic_arm_arm.set_start_state_to_current_state()
        self.robotic_arm_gripper.set_start_state_to_current_state()

        self.robotic_arm_arm.set_goal_state(robot_state=arm_state)
        self.robotic_arm_gripper.set_goal_state(robot_state=gripper_state)

        arm_plan_result = self.robotic_arm_arm.plan()
        gripper_plan_result = self.robotic_arm_gripper.plan()

        result = RoboticArmTask.Result()

        if arm_plan_result and gripper_plan_result:
            self.get_logger().info("Planner SUCCEED, moving the arm and the gripper")
            self.robotic_arm.execute(arm_plan_result.trajectory, controllers=[])
            self.robotic_arm.execute(gripper_plan_result.trajectory, controllers=[])
            self.get_logger().info("Goal succeeded")
            result.success = True
            goal_handle.succeed()
        else:
            self.get_logger().info("One or more planners failed!")
            result.success = False
            goal_handle.abort()
       
        return result


def main(args=None):
    rclpy.init(args=args)
    task_server = TaskServer()
    rclpy.spin(task_server)

if __name__ == "__main__":
    main()