#pragma once

#include "protocol.h"
#include <string>

namespace esphome {
namespace duepi {

class OneShotPermission {
 public:
  bool configured{false};
  uint32_t lifetime{30000};
  bool set_enabled(bool value) {
    this->enabled_ = value && this->configured;
    this->disarm();
    return this->enabled_;
  }
  bool enabled() const { return this->configured && this->enabled_; }
  bool arm(uint32_t now) {
    this->disarm();
    if (!this->enabled()) return false;
    this->armed_ = true;
    this->armed_at_ = now;
    return true;
  }
  bool reserve(uint32_t token, uint32_t now) {
    if (!this->enabled() || !this->armed_ || uint32_t(now - this->armed_at_) >= this->lifetime) return false;
    this->armed_ = false;
    this->reservation_ = token;
    this->reserved_ = true;
    return true;
  }
  bool permits(uint32_t token, uint32_t now) const {
    return this->enabled() && this->reserved_ && token == this->reservation_ &&
           uint32_t(now - this->armed_at_) < this->lifetime;
  }
  bool consume(uint32_t token, uint32_t now) {
    if (!this->permits(token, now)) return false;
    this->disarm();
    return true;
  }
  void disarm() { this->armed_ = false; this->reserved_ = false; }
 protected:
  bool enabled_{false};
  bool armed_{false};
  bool reserved_{false};
  uint32_t armed_at_{0};
  uint32_t reservation_{0};
};

inline bool custom_request(const std::string &text, bool raw, Request &request) {
  WireFrame frame{};
  if (raw) {
    if (text.size() != 6) return false;
    for (char character : text)
      if (!((character >= 'A' && character <= 'Z') || (character >= '0' && character <= '9'))) return false;
    if (!encode_payload(text.data(), text.size(), frame)) return false;
  } else {
    if (!encode_command(text.data(), text.size(), frame)) return false;
  }
  request.command = Command::CUSTOM;
  for (size_t index = 0; index < request.payload.size(); ++index) request.payload[index] = static_cast<char>(frame[index + 1]);
  return true;
}

}  // namespace duepi
}  // namespace esphome