#pragma once

#ifdef USE_DUEPI_TCP_EMULATOR
#include "esphome/components/socket/socket.h"
#include "esphome/components/switch/switch.h"
#include "esphome/components/uart/uart.h"
#include <array>

namespace esphome {
namespace duepi {

class DuepiHub;

class DuepiTCPEmulatorSwitch : public switch_::Switch {
 public:
  void set_parent(DuepiHub *parent) { this->parent_ = parent; }
 protected:
  void write_state(bool value) override;
  DuepiHub *parent_{nullptr};
};

class DuepiTCPEmulator {
 public:
  explicit DuepiTCPEmulator(DuepiHub *uart) : uart_(uart) {}
  bool start(uint16_t port);
  void stop();
  void loop();
  bool listening() const { return this->listener_ != nullptr; }
 protected:
  bool open_listener_();
  bool listener_failed_(const char *operation, int error);
  void close_listener_();
  void close_client_(const char *reason);
  void flush_();
  void log_bytes_(const char *direction, const uint8_t *data, size_t size);
  DuepiHub *uart_;
  std::unique_ptr<socket::ListenSocket> listener_;
  std::unique_ptr<socket::Socket> client_;
  std::array<uint8_t, 512> pending_{};
  size_t pending_size_{0};
  uint32_t last_progress_{0};
  uint32_t last_client_rx_{0};
  uint32_t last_listener_attempt_{0};
  uint16_t port_{2000};
  bool enabled_{false};
  bool listener_retry_pending_{false};
};

}  // namespace duepi
}  // namespace esphome
#endif