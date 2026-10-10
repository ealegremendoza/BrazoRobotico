import os
from launch import LaunchDescription
from launch.actions import DeclareLaunchArgument, IncludeLaunchDescription
from launch.substitutions import LaunchConfiguration
from ament_index_python.packages import get_package_share_directory


def generate_launch_description():
    port_arg = DeclareLaunchArgument(
        "port",
        default_value="/dev/ttyUSB0",
        description="Serial port of the ESP32: DevKitC WROOM (CP2102) /dev/ttyUSB0, 30-pin board (CH9102X) /dev/ttyACM0",
    )

    controller = IncludeLaunchDescription(
            os.path.join(
                get_package_share_directory("robotic_arm_controller"),
                "launch",
                "controller.launch.py"
            ),
            launch_arguments={"is_sim": "False", "port": LaunchConfiguration("port")}.items()
        )
    
    moveit = IncludeLaunchDescription(
            os.path.join(
                get_package_share_directory("robotic_arm_moveit"),
                "launch",
                "moveit.launch.py"
            ),
            launch_arguments={"is_sim": "False"}.items()
        )
    
    remote_interface = IncludeLaunchDescription(
            os.path.join(
                get_package_share_directory("robotic_arm_remote"),
                "launch",
                "remote_interface.launch.py"
            ),
            launch_arguments={"is_sim": "False"}.items()
        )
    
    return LaunchDescription([
        port_arg,
        controller,
        moveit,
        remote_interface,
    ])