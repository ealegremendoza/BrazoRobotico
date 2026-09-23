#!/usr/bin/env python3
import rclpy
from rclpy.node import Node
from std_msgs.msg import String
import serial


class SimpleSerialTransmitter(Node):
    def __init__(self):
        super().__init__("simple_serial_transmitter")

        self.declare_parameter("port", "/dev/ttyUSB0")
        self.declare_parameter("baudrate", 115200)

        self.port_ = self.get_parameter("port").value
        self.baudrate_ = self.get_parameter("baudrate").value

        self.sub_ = self.create_subscription(String, "serial_transmitter", self.msgCallback, 10)

        self.serial_port = serial.Serial(port=self.port_, baudrate=self.baudrate_, timeout=0.1)

    def msgCallback(self, msg):
        self.get_logger().info("New message received, publishing on serial: %s" % self.serial_port.name)
        data_to_send = (msg.data + "\n").encode("utf-8")
        self.serial_port.write(data_to_send)


def main():
    rclpy.init()

    simple_serial_transmitter = SimpleSerialTransmitter()
    rclpy.spin(simple_serial_transmitter)
    
    simple_serial_transmitter.destroy_node()
    rclpy.shutdown()


if __name__ == '__main__':
    main()