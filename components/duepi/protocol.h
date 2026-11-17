#pragma once

#include "esphome/core/defines.h"
#include <array>
#include <cstddef>
#include <cstdint>

namespace esphome {
namespace duepi {

constexpr size_t FRAME_SIZE = 10;
using WireFrame = std::array<uint8_t, FRAME_SIZE>;

struct Response {
  uint32_t data{0};
  std::array<char, 7> payload{};
  bool numeric{true};
  uint16_t word() const { return static_cast<uint16_t>(data >> 8); }
  uint8_t byte() const { return static_cast<uint8_t>(data >> 16); }
};

bool encode_payload(const char *payload, size_t length, WireFrame &frame);
bool encode_command(const char *command, size_t length, WireFrame &frame);
bool decode_response(const WireFrame &frame, Response &response);

class FrameParser {
 public:
  bool feed(uint8_t byte, uint32_t now, Response &response);
  void expire(uint32_t now);
  void clear() { this->used_ = 0; }
  size_t buffered() const { return this->used_; }
  uint32_t errors() const { return this->errors_; }
  uint32_t inter_byte_timeout{50};

 protected:
  WireFrame buffer_{};
  size_t used_{0};
  uint32_t last_byte_{0};
  uint32_t errors_{0};
};

enum class Command : uint8_t {
  PHASE, ROOM, D3000, D4000, FLUE, EXHAUST, ALARM, INPUTS, TARGET,
  PCB, FIRMWARE, BOARD, TOTAL_RUNTIME, SERVICE_RUNTIME, REQUESTED_POWER,
  FLUE_EXTRACTION_SPEED, EXTRACTION_SENSOR_LEVEL, FLUE_MOTOR_CORRECTION, PELLET_CORRECTION, CYCLE_TIME,
  INSTALLER_READ,
  OFF, START_9200, START_E670, START_E693, SET_TARGET, SET_LEVEL, RESET,
  INSTALLER_WRITE, CUSTOM, START_GENERIC
};

struct Request {
  Command command{Command::PHASE};
  uint16_t argument{0};
  uint32_t token{0};
  uint16_t parameter{0};
  std::array<char, 6> payload{};
};

bool is_write(Command command);
inline bool is_installer(Command command) {
  return command == Command::INSTALLER_READ || command == Command::INSTALLER_WRITE || command == Command::CUSTOM;
}
const char *command_name(Command command);
bool build_request(const Request &request, WireFrame &frame);
bool accepts(const Request &request, const Response &response);
bool known_phase(uint16_t phase);
const char *phase_name(uint16_t phase);
const char *alarm_name(uint16_t code, bool re8i);

}  // namespace duepi
}  // namespace esphome