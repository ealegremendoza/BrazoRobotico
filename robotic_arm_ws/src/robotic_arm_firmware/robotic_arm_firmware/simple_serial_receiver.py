#!/usr/bin/env python3
import serial
import rclpy
from rclpy.node import Node
from std_msgs.msg import String


class SimpleSerialReceiver(Node):
    def __init__(self):
        super().__init__("simple_serial_receiver")

        self.declare_parameter("port", "/dev/ttyUSB0")
        self.declare_parameter("baudrate", 115200)

        self.port_ = self.get_parameter("port").value
        self.baudrate_ = self.get_parameter("baudrate").value

        self.pub_ = self.create_publisher(String, "serial_receiver", 10)
        self.serial_port = serial.Serial(port=self.port_, baudrate=self.baudrate_, timeout=0.1)

        self.frequency_ = 0.01
        self.timer_ = self.create_timer(self.frequency_, self.timerCallback)

    def timerCallback(self):
        if rclpy.ok() and self.serial_port.is_open:
            try:
                data = self.serial_port.readline().strip()
                if not data:
                    return
                
                msg = String()
                msg.data = data.decode("utf-8")
                self.pub_.publish(msg)
                
            except UnicodeDecodeError as e:
                self.get_logger().warning(f"Invalid UTF-8 on serial: {e}")

            

def main():
    rclpy.init()

    simple_serial_receiver = SimpleSerialReceiver()
    rclpy.spin(simple_serial_receiver)
    
    simple_serial_receiver.destroy_node()
    rclpy.shutdown()


if __name__ == '__main__':
    main()