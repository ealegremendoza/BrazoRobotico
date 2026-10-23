#ifndef ROBOTIC_ARM_INTERFACE_H
#define ROBOTIC_ARM_INTERFACE_H

#include <rclcpp/rclcpp.hpp>
#include <hardware_interface/system_interface.hpp>
#include <libserial/SerialPort.h>
#include <rclcpp_lifecycle/state.hpp>
#include <rclcpp_lifecycle/node_interfaces/lifecycle_node_interface.hpp>

#include <vector>
#include <string>


namespace robotic_arm_controller
{

using CallbackReturn = rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface::CallbackReturn;

class RoboticArmInterface : public hardware_interface::SystemInterface
{
public:
  RoboticArmInterface();
  virtual ~RoboticArmInterface();

  // Implementing rclcpp_lifecycle::node_interfaces::LifecycleNodeInterface
  virtual CallbackReturn on_activate(const rclcpp_lifecycle::State &previous_state) override;
  virtual CallbackReturn on_deactivate(const rclcpp_lifecycle::State &previous_state) override;

  // Implementing hardware_interface::SystemInterface
  virtual CallbackReturn on_init(const hardware_interface::HardwareInfo &hardware_info) override;
  virtual std::vector<hardware_interface::StateInterface> export_state_interfaces() override;
  virtual std::vector<hardware_interface::CommandInterface> export_command_interfaces() override;
  virtual hardware_interface::return_type read(const rclcpp::Time & time, const rclcpp::Duration & period) override;
  virtual hardware_interface::return_type write(const rclcpp::Time & time, const rclcpp::Duration & period) override;

private:
  // Builds STX | LEN | CID | FS | payload | ETX | LRC (doc/md/bitacora-R09-ros2-control.md)
  static std::string buildFrame(char cid, const std::string &payload);
  // "M" payload: joint targets in mrad, fixed-width signed, separated by FS
  std::string buildMPayload(const std::vector<double> &positions_rad) const;
  // "S" param_init: URDF joint limits (info_.limits), mins then maxs, in "M" format
  std::string buildLimitsPayload() const;
  // Probes until the first "M" feedback, then sends "S" and waits for its reply ("M" with
  // the pose, or "E"). States and commands start at that pose. On failure, error says why
  bool syncWithArm(std::string &error);

  LibSerial::SerialPort serial_port_;
  std::string port_;
  std::vector<double> position_commands_;
  std::vector<double> position_states_;
  // Bytes received from the ESP32 that do not form a complete frame yet
  std::string rx_buffer_;
  // Set by read() when a valid "M" feedback updated position_states_
  bool m_received_ = false;
  // Set by read() when an "E" arrived, with its code (two digits)
  bool e_received_ = false;
  std::string e_code_;
};
}  // namespace robotic_arm_controller


#endif  // ROBOTIC_ARM_INTERFACE_H