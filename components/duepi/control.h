#pragma once

#include "state.h"
#include <cmath>

namespace esphome {
namespace duepi {

enum class FirmwareProfile : uint8_t { AUTO, FW_9200, FW_E670, FW_E693 };

class ControlPolicy {
 public:
  bool reset_enabled{false};
  uint16_t min_temperature{10};
  uint16_t max_temperature{25};
  FirmwareProfile profile{FirmwareProfile::AUTO};

  static bool shutdown_phase(uint16_t phase) {
    return phase == 0x0301 || phase == 0x0801 || phase == 0x0802;
  }

  bool start_request(const ObservedState &, Request &request, uint16_t level = 1) const {
    FirmwareProfile chosen = this->profile;
    request.command = chosen == FirmwareProfile::FW_9200 ? Command::START_9200 :
                      chosen == FirmwareProfile::FW_E670 ? Command::START_E670 :
                      chosen == FirmwareProfile::FW_E693 ? Command::START_E693 : Command::START_GENERIC;
    request.argument = level;
    return true;
  }

  bool permits(const Request &request, const ObservedState &state, uint32_t now, uint32_t lifetime) const {
    if (!is_write(request.command)) return true;
    if (!state.healthy(now, lifetime) ||
        !state.fresh(Command::FIRMWARE, now, lifetime) || !state.fresh(Command::BOARD, now, lifetime)) return false;
    uint16_t phase = static_cast<uint16_t>(state.fields[0].data >> 8);
    if (!known_phase(phase) || shutdown_phase(phase)) return false;
    if (request.command == Command::OFF) return true;
    if (request.command == Command::RESET) return this->reset_enabled;
    if (state.fields[static_cast<size_t>(Command::ALARM)].data >> 8) return false;
    Request ignition;
    if (!this->start_request(state, ignition)) return false;
    if (request.command == Command::START_9200 || request.command == Command::START_E670 ||
        request.command == Command::START_E693 || request.command == Command::START_GENERIC)
      return request.command == ignition.command && phase == 0 &&
             (request.command != Command::START_GENERIC || request.argument <= 5);
    if (request.command == Command::SET_LEVEL)
      return request.argument >= 1 && request.argument <= 5 && ((phase >> 8) == 1 || (phase >> 8) == 2);
    if (request.command == Command::SET_TARGET)
      return request.argument >= this->min_temperature && request.argument <= this->max_temperature;
    return false;
  }

  static bool target_request(float value, Request &request) {
    if (!std::isfinite(value) || value < 10 || value > 35 || std::floor(value) != value) return false;
    request.command = Command::SET_TARGET;
    request.argument = static_cast<uint16_t>(value);
    return true;
  }

  static bool verified_by(const Request &request, Command observed, const Response &response) {
    if (request.command == Command::SET_TARGET)
      return observed == Command::TARGET && response.word() == request.argument;
    if (request.command == Command::SET_LEVEL)
      return observed == Command::REQUESTED_POWER && response.word() == request.argument;
    if (observed != Command::PHASE) return false;
    if (request.command == Command::OFF) return response.word() == 0;
    if (request.command == Command::START_9200 || request.command == Command::START_E670 ||
      request.command == Command::START_E693 || request.command == Command::START_GENERIC)
      return known_phase(response.word()) && (response.word() >> 8) == 1;
    return false;
  }
};

}  // namespace duepi
}  // namespace esphome