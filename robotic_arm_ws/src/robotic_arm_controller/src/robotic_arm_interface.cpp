#include "robotic_arm_controller/robotic_arm_interface.hpp"
#include <hardware_interface/types/hardware_interface_type_values.hpp>
#include <pluginlib/class_list_macros.hpp>

#include <algorithm>
#include <cmath>
#include <cstdio>


namespace robotic_arm_controller
{

// Poner todas las constantes aqui dentro
namespace
{
// PC <-> ESP32 frame bytes (see doc/md/bitacora-R09-ros2-control.md).
// Not named STX/ETX/FS: FS is a macro in <sys/reg.h>.
constexpr char kStx = '\x02';
constexpr char kEtx = '\x03';
constexpr char kFieldSeparator = '\x1C';
// Joint range sent in "M": +-pi rad
constexpr long kMaxJointMrad = 3142;
}  // namespace
  
RoboticArmInterface::RoboticArmInterface()
{
}

RoboticArmInterface::~RoboticArmInterface()
{
  if (serial_port_.IsOpen())
  {
    try
    {
      serial_port_.Close();
    }
    catch (...)
    {
      RCLCPP_FATAL_STREAM(rclcpp::get_logger("RoboticArmInterface"),
                          "Something went wrong while closing connection with port " << port_);
    }
  }
}

CallbackReturn RoboticArmInterface::on_init(const hardware_interface::HardwareInfo &hardware_info)
{
  CallbackReturn result = hardware_interface::SystemInterface::on_init(hardware_info);
  if (result != CallbackReturn::SUCCESS)
  {
    return result;
  }

  try
  {
    port_ = info_.hardware_parameters.at("port");
  }
  catch (const std::out_of_range &e)
  {
    RCLCPP_FATAL(rclcpp::get_logger("RoboticArmInterface"), "No Serial Port provided! Aborting");
    return CallbackReturn::FAILURE;
  }

  position_commands_.reserve(info_.joints.size()); //reserv reserva espacio para el vector
  position_states_.reserve(info_.joints.size());

  return CallbackReturn::SUCCESS;
}

/* El proposito de esta funcion es crear un vector de objetos de tipo state.
 * El proposito es comunicar la interface que necesitamos usar para conocer el estado de cada joint del brazo.
 */
std::vector<hardware_interface::StateInterface> RoboticArmInterface::export_state_interfaces()
{
  std::vector<hardware_interface::StateInterface> state_interfaces;

  // Provide only a position Interafce
  for (size_t i = 0; i < info_.joints.size(); i++)
  {
    // emplace_back para agregar (construir) un nuevo objeto
    // esta es una forma de declarar cada joint del robot en el StateInterface
    state_interfaces.emplace_back(hardware_interface::StateInterface(
        info_.joints[i].name, hardware_interface::HW_IF_POSITION, &position_states_[i]));
  }

  return state_interfaces;
}

/* El proposito de esta funcion es comunicar la interface que vamos a usar para enviar comandos
 * a cada joint.
 */
std::vector<hardware_interface::CommandInterface> RoboticArmInterface::export_command_interfaces()
{
  std::vector<hardware_interface::CommandInterface> command_interfaces;

  // Provide only a position Interafce
  for (size_t i = 0; i < info_.joints.size(); i++)
  {
    command_interfaces.emplace_back(hardware_interface::CommandInterface(
        info_.joints[i].name, hardware_interface::HW_IF_POSITION, &position_commands_[i]));
  }

  return command_interfaces;
}


CallbackReturn RoboticArmInterface::on_activate(const rclcpp_lifecycle::State &previous_state)
{
  RCLCPP_INFO(rclcpp::get_logger("RoboticArmInterface"), "Starting robot hardware ...");

  // Reset commands and states
  // Cada elemento representa la posicion de un joint
  position_commands_ = { 0.0, 0.0, 0.0, 0.0, 0.0, 0.0 };
  position_states_ = { 0.0, 0.0, 0.0, 0.0, 0.0 , 0.0 };

  try
  {
    serial_port_.Open(port_);
    serial_port_.SetBaudRate(LibSerial::BaudRate::BAUD_115200);
  }
  catch (...)
  {
    RCLCPP_FATAL_STREAM(rclcpp::get_logger("RoboticArmInterface"),
                        "Something went wrong while interacting with port " << port_);
    return CallbackReturn::FAILURE;
  }

  RCLCPP_INFO(rclcpp::get_logger("RoboticArmInterface"),
              "Hardware started, ready to take commands");
  return CallbackReturn::SUCCESS;
}


CallbackReturn RoboticArmInterface::on_deactivate(const rclcpp_lifecycle::State &previous_state)
{
  RCLCPP_INFO(rclcpp::get_logger("RoboticArmInterface"), "Stopping robot hardware ...");

  if (serial_port_.IsOpen())
  {
    try
    {
      serial_port_.Close();
    }
    catch (...)
    {
      RCLCPP_FATAL_STREAM(rclcpp::get_logger("RoboticArmInterface"),
                          "Something went wrong while closing connection with port " << port_);
    }
  }

  RCLCPP_INFO(rclcpp::get_logger("RoboticArmInterface"), "Hardware stopped");
  return CallbackReturn::SUCCESS;
}


hardware_interface::return_type RoboticArmInterface::read(const rclcpp::Time &time,
                                                          const rclcpp::Duration &period)
{
  // Open Loop Control - assuming the robot is always where we command to be
  // Los servos que uso si reportan la posicion. habria que usar eso.
  // aca  esta asumiendo que no hay feedback de parte de los servos.
  position_states_ = position_commands_;
  return hardware_interface::return_type::OK;
}

hardware_interface::return_type RoboticArmInterface::write(const rclcpp::Time &time,
                                                           const rclcpp::Duration &period)
{
  // Payload of "M": joint targets in mrad, fixed-width signed, separated by FS.
  // Offsets and servo calibration are handled by the ESP32.
  std::string payload;
  for (size_t i = 0; i < position_commands_.size(); i++)
  {
    const long mrad = std::lround(position_commands_.at(i) * 1000.0);
    const long clamped = std::clamp(mrad, -kMaxJointMrad, kMaxJointMrad);
    if (clamped != mrad)
    {
      RCLCPP_WARN_STREAM(rclcpp::get_logger("RoboticArmInterface"),
                         "Joint " << i << " command " << mrad << " mrad out of range, clamped to "
                                  << clamped);
    }

    char field[6];  // 5 chars + '\0'
    std::snprintf(field, sizeof(field), "%+05ld", clamped);
    if (i > 0)
    {
      payload += kFieldSeparator;
    }
    payload += field;
  }

  // Frame: STX | LEN | CID | FS | payload | ETX | LRC
  std::string body;
  body += 'M';
  body += kFieldSeparator;
  body += payload;

  // LEN counts CID + FS + payload + ETX + LRC (ETX and LRC are appended below)
  char len[5];  // 4 digits + '\0'
  std::snprintf(len, sizeof(len), "%04zu", body.size() + 2);

  std::string msg;
  msg += kStx;
  msg += len;
  msg += body;
  msg += kEtx;

  // LRC: XOR of everything except STX
  char lrc = 0;
  for (size_t i = 1; i < msg.size(); i++)
  {
    lrc ^= msg[i];
  }
  msg += lrc;

  try
  {
    // DEBUG: runs every cycle (50 Hz) and the frame has non-printable bytes
    RCLCPP_DEBUG_STREAM(rclcpp::get_logger("RoboticArmInterface"), "Sending frame, payload " << payload);
    serial_port_.Write(msg);
  }
  catch (...)
  {
    RCLCPP_ERROR_STREAM(rclcpp::get_logger("RoboticArmInterface"),
                        "Something went wrong while sending the payload "
                            << payload << " to the port " << port_);
    return hardware_interface::return_type::ERROR;
  }

  return hardware_interface::return_type::OK;
}
}  // namespace robotic_arm_controller

// Registrar plugin
PLUGINLIB_EXPORT_CLASS(robotic_arm_controller::RoboticArmInterface, hardware_interface::SystemInterface)
