import os
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument
from launch.substitutions import LaunchConfiguration
from launch_ros.actions import Node
from moveit_configs_utils import MoveItConfigsBuilder
from ament_index_python.packages import get_package_share_directory


def generate_launch_description():

    is_sim_arg = DeclareLaunchArgument(
        "is_sim",
        default_value="True"
    )

    is_sim = LaunchConfiguration("is_sim")

    moveit_config = (
        MoveItConfigsBuilder("robotic_arm", package_name="robotic_arm_moveit")
        .robot_description(file_path=os.path.join(
            get_package_share_directory("robotic_arm_description"),
            "urdf",
            "robotic_arm.urdf.xacro"
            )
        )
        .robot_description_semantic(file_path="config/robotic_arm.srdf")
        .trajectory_execution(file_path="config/moveit_controllers.yaml")
        .moveit_cpp(file_path="config/planning_python_api.yaml")
        .to_moveit_configs()
    )

    task_server_node = Node(
        package="robotic_arm_remote",
        executable="task_server",
        parameters=[moveit_config.to_dict(),
                    {"use_sim_time": is_sim}]
    )

    return LaunchDescription([
        is_sim_arg,
        task_server_node,
    ])
