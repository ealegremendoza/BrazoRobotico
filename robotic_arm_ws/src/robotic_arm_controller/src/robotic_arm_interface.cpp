#include "robotic_arm_controller/robotic_arm_interface.hpp"
#include <hardware_interface/types/hardware_interface_type_values.hpp>
#include <pluginlib/class_list_macros.hpp>


namespace robotic_arm_controller
{

std::string compensateZeros(const int value)
{
  std::string compensate_zeros = "";
  if(value < 10){
    compensate_zeros = "00";
  } else if(value < 100){
    compensate_zeros = "0";
  } else {
    compensate_zeros = "";
  }
  return compensate_zeros;
}
  
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
  prev_position_commands_.reserve(info_.joints.size());

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
  prev_position_commands_ = { 0.0, 0.0, 0.0, 0.0, 0.0 , 0.0 };
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
  if (position_commands_ == prev_position_commands_)
  {
    // Nothing changed, do not send any command
    return hardware_interface::return_type::OK;
  }

  // aca se adopta el siguiente protocolo  bDegree,sDegree,eDegree,gDegree.
  // donde b significa base, s: shoulder, e: elbow y g:grip
  std::string msg;
  int base = static_cast<int>(((position_commands_.at(0) + (M_PI / 2)) * 180) / M_PI);
  msg.append("b");
  msg.append(compensateZeros(base));
  msg.append(std::to_string(base));
  msg.append(",");
  int shoulder = 180 - static_cast<int>(((position_commands_.at(1) + (M_PI / 2)) * 180) / M_PI);
  msg.append("s");
  msg.append(compensateZeros(shoulder));
  msg.append(std::to_string(shoulder));
  msg.append(",");
  int elbow = static_cast<int>(((position_commands_.at(2) + (M_PI / 2)) * 180) / M_PI);
  msg.append("e");
  msg.append(compensateZeros(elbow));
  msg.append(std::to_string(elbow));
  msg.append(",");
  int gripper = static_cast<int>(((-position_commands_.at(3)) * 180) / (M_PI / 2));
  msg.append("g");
  msg.append(compensateZeros(gripper));
  msg.append(std::to_string(gripper));
  msg.append(",");

  try
  {
    RCLCPP_INFO_STREAM(rclcpp::get_logger("RoboticArmInterface"), "Sending new command " << msg);
    serial_port_.Write(msg);
  }
  catch (...)
  {
    RCLCPP_ERROR_STREAM(rclcpp::get_logger("RoboticArmInterface"),
                        "Something went wrong while sending the message "
                            << msg << " to the port " << port_);
    return hardware_interface::return_type::ERROR;
  }

  prev_position_commands_ = position_commands_;

  return hardware_interface::return_type::OK;
}
}  // namespace robotic_arm_controller

// Registrar plugin
PLUGINLIB_EXPORT_CLASS(robotic_arm_controller::RoboticArmInterface, hardware_interface::SystemInterface)
