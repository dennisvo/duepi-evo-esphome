#pragma once

#include "protocol.h"
#include <array>
#include <cstdio>

namespace esphome {
namespace duepi {

constexpr size_t FIELD_COUNT = 15;

struct ObservedField {
  uint32_t data{0};
  uint32_t updated_at{0};
  bool valid{false};
  bool fresh(uint32_t now, uint32_t lifetime) const {
    return this->valid && uint32_t(now - this->updated_at) < lifetime;
  }
};

class ObservedState {
 public:
  bool update(Command command, const Response &response, uint32_t now) {
    size_t index = static_cast<size_t>(command);
    if (index >= FIELD_COUNT || !accepts({command}, response)) return false;
    if ((command == Command::TARGET && (response.word() < 10 || response.word() > 35)) ||
      (command == Command::REQUESTED_POWER && (response.word() < 1 || response.word() > 5))) {
      this->fields[index].valid = false;
      return false;
    }
    this->fields[index] = {response.data, now, true};
    if (command == Command::FIRMWARE) {
      if (response.payload[0] != 0) {
        for (size_t offset = 0; offset < 4; ++offset) this->firmware[offset] = response.payload[offset];
        this->firmware[4] = 0;
      } else {
        std::snprintf(this->firmware.data(), this->firmware.size(), "%04X", unsigned(response.word()));
      }
    }
    return true;
  }
  bool healthy(uint32_t now, uint32_t lifetime) const {
    return this->fresh(Command::PHASE, now, lifetime) && this->fresh(Command::ROOM, now, lifetime) &&
           this->fresh(Command::ALARM, now, lifetime);
  }
  bool fresh(Command command, uint32_t now, uint32_t lifetime) const {
    auto index = static_cast<size_t>(command);
    return index < FIELD_COUNT && this->fields[index].fresh(now, lifetime);
  }
  void invalidate() {
    for (auto &field : this->fields) field.valid = false;
  }
  std::array<ObservedField, FIELD_COUNT> fields{};
  std::array<char, 5> firmware{};
};

inline float numeric_value(Command command, uint32_t data) {
  if (command == Command::TOTAL_RUNTIME || command == Command::SERVICE_RUNTIME) return static_cast<float>(data);
  if (command == Command::BOARD) return static_cast<float>(data >> 16);
  float value = static_cast<float>(data >> 8);
  if (command == Command::ROOM) return value / 10.0f;
  if (command == Command::EXHAUST) return value * 10.0f;
  return value;
}

}  // namespace duepi
}  // namespace esphome