#include "robotic_arm_controller/robotic_arm_interface.hpp"
#include <hardware_interface/types/hardware_interface_type_values.hpp>
#include <pluginlib/class_list_macros.hpp>

#include <algorithm>
#include <cctype>
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
// LEN: 4 ASCII digits, counts CID + FS + payload + ETX + LRC
constexpr size_t kLenDigits = 4;
constexpr size_t kMinLen = 4;   // CID + FS + ETX + LRC, empty payload
constexpr size_t kMaxLen = 64;  // parser sanity cap, a 6-joint "M" has LEN 39
// Joint field in "M": sign + 4 digits, e.g. "+1571"
constexpr size_t kJointFieldLen = 5;
// Safety net only: read() asks for bytes already available, so it never waits
constexpr size_t kReadTimeoutMs = 1;

// Parses an "M" payload (j1 FS j2 FS ... FS jn, mrad) into radians.
// All or nothing: on any malformed field, positions is left untouched.
bool parseJointPositions(const std::string &payload, std::vector<double> &positions)
{
  const size_t n = positions.size();
  if (n == 0 || payload.size() != n * kJointFieldLen + (n - 1))
  {
    return false;
  }

  std::vector<double> parsed;
  parsed.reserve(n);
  for (size_t i = 0; i < n; i++)
  {
    const size_t offset = i * (kJointFieldLen + 1);
    if (i > 0 && payload[offset - 1] != kFieldSeparator)
    {
      return false;
    }

    const char sign = payload[offset];
    if (sign != '+' && sign != '-')
    {
      return false;
    }

    long mrad = 0;
    for (size_t j = 1; j < kJointFieldLen; j++)
    {
      const char c = payload[offset + j];
      if (!std::isdigit(static_cast<unsigned char>(c)))
      {
        return false;
      }
      mrad = mrad * 10 + (c - '0');
    }
    if (sign == '-')
    {
      mrad = -mrad;
    }
    parsed.push_back(mrad / 1000.0);
  }

  positions = parsed;
  return true;
}
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
  rx_buffer_.clear();

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
  // Non-blocking read: only fetch the bytes already waiting in the OS buffer.
  // LibSerial Read() with msTimeout = 0 blocks until all requested bytes arrive.
  try
  {
    const int available = serial_port_.GetNumberOfBytesAvailable();
    if (available > 0)
    {
      std::string chunk;
      try
      {
        serial_port_.Read(chunk, static_cast<size_t>(available), kReadTimeoutMs);
      }
      catch (const LibSerial::ReadTimeout &)
      {
        // Keep whatever arrived, the rest comes in the next cycle
      }
      rx_buffer_ += chunk;
    }
  }
  catch (...)
  {
    RCLCPP_ERROR_STREAM(rclcpp::get_logger("RoboticArmInterface"),
                        "Something went wrong while reading from the port " << port_);
    return hardware_interface::return_type::ERROR;
  }

  // Frame: STX | LEN | CID | FS | payload | ETX | LRC
  // Invalid frame -> drop 1 byte and resync. Incomplete frame -> wait for next read().
  while (true)
  {
    // 1. Drop everything before the first STX
    const size_t stx = rx_buffer_.find(kStx);
    if (stx == std::string::npos)
    {
      rx_buffer_.clear();
      break;
    }
    rx_buffer_.erase(0, stx);

    // 2. LEN not received yet
    if (rx_buffer_.size() < 1 + kLenDigits)
    {
      break;
    }

    // 3. LEN must be 4 digits within range. The STX may be a false one (e.g. an LRC equal to 0x02)
    size_t len = 0;
    bool len_ok = true;
    for (size_t i = 1; i <= kLenDigits; i++)
    {
      const char c = rx_buffer_[i];
      if (!std::isdigit(static_cast<unsigned char>(c)))
      {
        len_ok = false;
        break;
      }
      len = len * 10 + (c - '0');
    }
    if (!len_ok || len < kMinLen || len > kMaxLen)
    {
      rx_buffer_.erase(0, 1);
      continue;
    }

    // 4. Frame not complete yet
    const size_t frame_size = 1 + kLenDigits + len;
    if (rx_buffer_.size() < frame_size)
    {
      break;
    }

    // 5. ETX in place and LRC (XOR from LEN to ETX) matches
    char lrc = 0;
    for (size_t i = 1; i < frame_size - 1; i++)
    {
      lrc ^= rx_buffer_[i];
    }
    if (rx_buffer_[frame_size - 2] != kEtx || rx_buffer_[frame_size - 1] != lrc)
    {
      rx_buffer_.erase(0, 1);
      continue;
    }

    // 6. Valid frame: take it out of the buffer and process it
    const size_t body_start = 1 + kLenDigits;
    const char cid = rx_buffer_[body_start];
    const bool fs_ok = rx_buffer_[body_start + 1] == kFieldSeparator;
    const std::string payload = rx_buffer_.substr(body_start + 2, len - kMinLen);
    rx_buffer_.erase(0, frame_size);

    if (!fs_ok)
    {
      RCLCPP_WARN(rclcpp::get_logger("RoboticArmInterface"), "Frame without FS after CID, dropped");
      continue;
    }

    switch (cid)
    {
      case 'M':
        // If several "M" arrived, the last one wins
        if (!parseJointPositions(payload, position_states_))
        {
          RCLCPP_WARN_STREAM(rclcpp::get_logger("RoboticArmInterface"),
                             "Malformed M payload, dropped: " << payload);
        }
        break;
      case 'E':
        RCLCPP_WARN_STREAM(rclcpp::get_logger("RoboticArmInterface"), "ESP32 event: " << payload);
        break;
      default:
        RCLCPP_WARN_STREAM(rclcpp::get_logger("RoboticArmInterface"),
                           "Unknown CID '" << cid << "', frame dropped");
        break;
    }
  }

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
