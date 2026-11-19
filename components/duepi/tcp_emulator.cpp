#include "duepi.h"
#ifdef USE_DUEPI_TCP_EMULATOR
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include <algorithm>
#include <cerrno>
#include <cstring>

namespace esphome {
namespace duepi {

static const char *const TAG = "duepi.tcp_emulator";

bool DuepiHub::set_tcp_emulator_enabled(bool value) {
  if (value == this->tcp_emulator_enabled_) return value;
  if (value) this->tcp_emulator_.start(this->tcp_emulator_port_);
  this->tcp_emulator_enabled_ = value;
  if (!value) this->tcp_emulator_.stop();
  this->engine_.resynchronize(millis());
  this->state_.invalidate();
  this->cycle_active_ = true;
  this->cursor_ = 0;
  this->tcp_emulator_transition_ = true;
  this->tcp_emulator_transition_at_ = this->tcp_emulator_last_rx_ = millis();
#ifdef USE_DUEPI_INSTALLER
  this->set_installer_access(false);
  if (this->privilege_switches_[0] != nullptr) this->privilege_switches_[0]->publish_state(false);
#endif
  for (auto *entity : this->numeric_) if (entity != nullptr) entity->publish_state(NAN);
  for (size_t index : {size_t(0), size_t(1), size_t(2), size_t(3), size_t(9)})
    if (this->text_[index] != nullptr) this->text_[index]->publish_state("Unknown");
  this->publish_(millis());
  if (this->text_[4] != nullptr)
    this->text_[4]->publish_state(value ? "TCP emulator enabled; waiting for UART handover" :
                                        "TCP emulator disabled; waiting for UART resynchronization");
  ESP_LOGI(TAG, "TCP emulator %s; pending work cancelled, UART handover draining", value ? "enabled" : "disabled");
  return value;
}

void DuepiTCPEmulatorSwitch::write_state(bool value) {
  this->publish_state(this->parent_->set_tcp_emulator_enabled(value));
}

bool DuepiTCPEmulator::start(uint16_t port) {
  this->stop();
  this->enabled_ = true;
  this->port_ = port;
  return this->open_listener_();
}

bool DuepiTCPEmulator::open_listener_() {
  this->last_listener_attempt_ = millis();
  this->listener_retry_pending_ = true;
  this->listener_ = socket::socket_ip_loop_monitored(SOCK_STREAM, 0);
  if (this->listener_ == nullptr) return this->listener_failed_("create", errno);
  if (this->listener_->setblocking(false) != 0) return this->listener_failed_("nonblocking setup", errno);
  int reuse = 1;
  if (this->listener_->setsockopt(SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse)) != 0)
    return this->listener_failed_("SO_REUSEADDR", errno);
  sockaddr_storage address{};
  socklen_t size = socket::set_sockaddr_any(reinterpret_cast<sockaddr *>(&address), sizeof(address), this->port_);
  if (size == 0) return this->listener_failed_("address setup", EINVAL);
  if (this->listener_->bind(reinterpret_cast<sockaddr *>(&address), size) != 0)
    return this->listener_failed_("bind", errno);
  if (this->listener_->listen(1) != 0) return this->listener_failed_("listen", errno);
  this->listener_retry_pending_ = false;
  ESP_LOGW(TAG, "Raw TCP listener on port %u: unauthenticated stove access; trusted LAN only", unsigned(this->port_));
  return true;
}

bool DuepiTCPEmulator::listener_failed_(const char *operation, int error) {
  ESP_LOGW(TAG, "TCP listener %s failed on port %u: errno=%d; retry in 5s, UART remains exclusive",
           operation, unsigned(this->port_), error);
  this->last_listener_attempt_ = millis();
  this->listener_retry_pending_ = true;
  this->close_client_("listener failed; reconnect required");
  this->close_listener_();
  return false;
}

void DuepiTCPEmulator::close_listener_() {
  if (this->listener_ != nullptr) {
    this->listener_->close();
    this->listener_.reset();
  }
}

void DuepiTCPEmulator::close_client_(const char *reason) {
  if (this->client_ != nullptr) {
    ESP_LOGI(TAG, "Client disconnected: %s", reason);
    this->client_->close();
    this->client_.reset();
  }
  this->pending_size_ = 0;
}

void DuepiTCPEmulator::stop() {
  this->enabled_ = false;
  this->listener_retry_pending_ = false;
  this->close_client_("emulator disabled");
  this->close_listener_();
}

void DuepiTCPEmulator::log_bytes_(const char *direction, const uint8_t *data, size_t size) {
  static const char HEX[] = "0123456789ABCDEF";
  char hex[128 * 3 + 1];
  for (size_t index = 0; index < size; ++index) {
    hex[index * 3] = HEX[data[index] >> 4];
    hex[index * 3 + 1] = HEX[data[index] & 15];
    hex[index * 3 + 2] = ' ';
  }
  hex[size * 3] = 0;
  ESP_LOGV(TAG, "%lu ms %s [%u]: %s", static_cast<unsigned long>(millis()), direction, unsigned(size), hex);
}

void DuepiTCPEmulator::flush_() {
  if (this->client_ == nullptr || this->pending_size_ == 0) return;
  auto written = this->client_->write(this->pending_.data(), this->pending_size_);
  if (written > 0) {
    this->pending_size_ -= static_cast<size_t>(written);
    std::memmove(this->pending_.data(), this->pending_.data() + written, this->pending_size_);
    this->last_progress_ = millis();
  } else if (written == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
    if (written < 0) ESP_LOGW(TAG, "TCP write failed: errno=%d", errno);
    this->close_client_("TCP write failed");
  }
}

void DuepiTCPEmulator::loop() {
  if (!this->enabled_) return;
  if (this->listener_ == nullptr &&
      (!this->listener_retry_pending_ || uint32_t(millis() - this->last_listener_attempt_) >= 5000))
    this->open_listener_();
  uint8_t data[128];
  if (this->client_ != nullptr) {
    size_t capacity = std::min(sizeof(data), this->uart_->parent_->available_for_write());
    if (capacity != 0) {
      auto received = this->client_->read(data, capacity);
      if (received > 0) {
        this->last_client_rx_ = millis();
        this->uart_->write_array(data, static_cast<size_t>(received));
        this->log_bytes_("TCP->UART", data, static_cast<size_t>(received));
      } else if (received == 0 || (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR)) {
        if (received < 0) ESP_LOGW(TAG, "TCP read failed: errno=%d", errno);
        this->close_client_("TCP closed or read failed");
      }
    }
    this->flush_();
    if (this->client_ != nullptr && uint32_t(millis() - this->last_client_rx_) >= 30000)
      this->close_client_("idle timeout: no app input for 30 seconds");
  }
  if (this->client_ == nullptr) {
    uint8_t discarded = 0;
    for (size_t budget = 0; budget < sizeof(data) && this->uart_->available() != 0; ++budget)
      if (!this->uart_->read_byte(&discarded)) break;
    if (this->uart_->available() != 0) return;
  }
  if (this->listener_ == nullptr) return;
  auto incoming = this->listener_->accept_loop_monitored(nullptr, nullptr);
  if (incoming == nullptr && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR && errno != ECONNABORTED) {
    this->listener_failed_("accept", errno);
    return;
  }
  if (incoming != nullptr) {
    int nodelay = 1;
    if (this->client_ != nullptr) {
      ESP_LOGW(TAG, "Rejected additional TCP client: existing session owns UART; toggle emulator off/on to clear it");
      incoming->close();
    } else if (incoming->setblocking(false) != 0 ||
        incoming->setsockopt(IPPROTO_TCP, TCP_NODELAY, &nodelay, sizeof(nodelay)) != 0) {
      ESP_LOGW(TAG, "Rejected TCP client: socket setup failed, errno=%d", errno);
      incoming->close();
    } else {
      this->client_ = std::move(incoming);
      this->last_progress_ = this->last_client_rx_ = millis();
      ESP_LOGI(TAG, "Client connected; raw UART ownership remains exclusive");
    }
  }
  if (this->client_ == nullptr) return;
  size_t received = 0;
  while (received < sizeof(data) && this->uart_->available() != 0) {
    if (!this->uart_->read_byte(&data[received])) break;
    ++received;
  }
  if (received != 0) {
    this->log_bytes_("UART->TCP", data, received);
    if (this->pending_size_ + received > this->pending_.size()) {
      this->close_client_("UART buffer overflow; bytes discarded, no replay");
      return;
    }
    if (this->pending_size_ == 0) this->last_progress_ = millis();
    std::memcpy(this->pending_.data() + this->pending_size_, data, received);
    this->pending_size_ += received;
  }
  this->flush_();
  if (this->pending_size_ != 0 && uint32_t(millis() - this->last_progress_) >= 3000)
    this->close_client_("TCP backpressure timeout; bytes discarded, no replay");
}

}  // namespace duepi
}  // namespace esphome
#endif