#include "duepi.h"
#ifdef USE_DUEPI_INSTALLER
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include <cstdio>
#include <cmath>

namespace esphome {
namespace duepi {

static const char *const TAG = "duepi";

void DuepiStandbySwitch::write_state(bool value) {
  this->parent_->request_standby(value);
  if (this->known_) this->publish_state(this->state);
}

bool DuepiHub::request_standby(bool value) {
  if (this->standby_switch_ == nullptr || !this->installer_parameter_allowed_(this->standby_parameter_) ||
      !this->installer_write_ready_() || this->installer_verifying_) {
    this->reject_control("Standby change rejected: not configured, parameter range, busy, stale, unknown phase or ignition");
    return false;
  }
  Request request{Command::INSTALLER_WRITE};
  request.parameter = this->standby_parameter_;
  request.argument = value ? 1 : 0;
  request.token = ++this->token_;
  request.standby = true;
  if (!this->standby_permission_.arm(millis()) || !this->standby_permission_.reserve(request.token, millis()))
    return false;
  return this->engine_.enqueue(request, millis());
}

void DuepiInstallerValue::control(float value) {
  if (std::isfinite(value) && std::floor(value) == value && value >= 0 && value <= 255)
    this->publish_state(value);
  else
    this->publish_state(this->state);
}

void DuepiInstallerButton::press_action() {
  this->parent_->installer_selected(this->operation_ == 1);
}

bool DuepiHub::installer_selected(bool write) {
  if (this->installer_numbers_[0] == nullptr || this->installer_numbers_[1] == nullptr) return false;
  int parameter = static_cast<int>(this->installer_numbers_[0]->state);
  if (!write) return this->installer_read(parameter);
  return this->request_installer_write(parameter, static_cast<int>(this->installer_numbers_[1]->state));
}

#if defined(USE_API) && defined(USE_API_CUSTOM_SERVICES)
void DuepiInstallerAPI::setup() {
  this->register_service(&DuepiInstallerAPI::read, this->prefix_ + "_installer_read", {"parameter"});
  this->register_service(&DuepiInstallerAPI::write, this->prefix_ + "_installer_write", {"parameter", "value"});
  this->register_service(&DuepiInstallerAPI::command, this->prefix_ + "_send_command", {"command"});
  this->register_service(&DuepiInstallerAPI::raw, this->prefix_ + "_send_raw", {"payload"});
}
void DuepiInstallerAPI::read(int32_t parameter) { this->parent_->installer_read(parameter); }
void DuepiInstallerAPI::write(int32_t parameter, int32_t value) {
  this->parent_->request_installer_write(parameter, value);
}
void DuepiInstallerAPI::command(std::string command) { this->parent_->request_custom(command, false); }
void DuepiInstallerAPI::raw(std::string payload) { this->parent_->request_custom(payload, true); }
#endif

bool DuepiHub::request_installer_write(int parameter, int value) {
  return this->arm_installer_write() && this->installer_write(parameter, value);
}

bool DuepiHub::request_custom(const std::string &command, bool raw) {
  this->custom_response_token_ = 0;
  if (this->text_[6] != nullptr) this->text_[6]->publish_state("Unavailable");
  return this->arm_custom_command() && this->send_custom(command, raw);
}

bool DuepiHub::set_installer_access(bool value) {
  bool enabled = this->set_installer_enabled(value);
  this->set_custom_enabled(enabled);
  return enabled;
}

bool DuepiHub::set_installer_enabled(bool value) {
  bool was_verifying = this->installer_verifying_;
  bool enabled = this->installer_permission_.set_enabled(value);
  ESP_LOGI(TAG, "Installer access %s; one-shot write permission cleared", enabled ? "enabled" : "disabled");
  if (!enabled) this->engine_.cancel_installer();
  if (this->text_[5] != nullptr && !(was_verifying && !this->installer_verifying_))
    this->text_[5]->publish_state(enabled ? "Installer enabled" : "Installer disabled");
  return enabled;
}

bool DuepiHub::set_custom_enabled(bool value) {
  bool enabled = this->custom_permission_.set_enabled(value);
  ESP_LOGI(TAG, "Custom command access %s; one-shot permission cleared", enabled ? "enabled" : "disabled");
  if (!enabled) this->engine_.cancel_installer();
  return enabled;
}

bool DuepiHub::privileged_ready_() const {
  uint32_t now = millis();
  return this->sync_mask_ == 31 && this->state_.healthy(now, this->stale_after_) &&
         this->state_.fresh(Command::FIRMWARE, now, this->stale_after_) &&
         this->state_.fresh(Command::BOARD, now, this->stale_after_) && !this->engine_.recovering();
}

bool DuepiHub::installer_write_ready_() const {
  uint16_t phase = this->state_.fields[0].data >> 8;
  return this->privileged_ready_() && known_phase(phase) && (phase >> 8) != 1;
}

bool DuepiHub::installer_request_allowed_(const Request &request) const {
  if (request.command == Command::INSTALLER_READ || request.command == Command::INSTALLER_WRITE)
    return this->installer_parameter_allowed_(request.parameter);
  if (request.command != Command::CUSTOM || (request.payload[0] != 'L' && request.payload[0] != 'S')) return true;
  int parameter = 0;
  for (size_t index = 1; index <= 2; ++index) {
    char character = request.payload[index];
    int digit = character >= '0' && character <= '9' ? character - '0' :
                character >= 'A' && character <= 'F' ? character - 'A' + 10 : -1;
    if (digit < 0) return false;
    parameter = parameter * 16 + digit;
  }
  return this->installer_parameter_allowed_(parameter);
}

bool DuepiHub::arm_installer_write() {
  bool armed = this->installer_write_ready_() &&
               this->installer_permission_.arm(millis());
  ESP_LOGD(TAG, "Installer one-shot arming %s (lifetime %lu ms; requires enabled, fresh, synchronized access)",
           armed ? "accepted" : "rejected", static_cast<unsigned long>(this->installer_permission_.lifetime));
  if (!armed) {
    const char *reason = "Installer write rejected: disabled, unsynchronized, stale, unknown phase or ignition";
    if (this->text_[5] != nullptr) this->text_[5]->publish_state(reason);
    this->reject_control(reason);
  }
  return armed;
}

bool DuepiHub::arm_custom_command() {
  bool armed = !this->write_blocked_(Command::CUSTOM) && this->privileged_ready_() &&
               this->custom_permission_.arm(millis());
  ESP_LOGD(TAG, "Custom one-shot arming %s (lifetime %lu ms; requires enabled, fresh, synchronized access)",
           armed ? "accepted" : "rejected", static_cast<unsigned long>(this->custom_permission_.lifetime));
  if (!armed) this->reject_control("Custom command rejected: disabled, unsynchronized, stale, shutdown or OFF pending");
  return armed;
}

bool DuepiHub::installer_read(int parameter) {
  if (!this->installer_parameter_allowed_(parameter) || !this->installer_permission_.enabled() ||
      !this->privileged_ready_() || this->installer_verifying_) {
    const char *reason = "Installer read rejected: range, disabled, busy or stale";
    if (this->text_[5] != nullptr) this->text_[5]->publish_state(reason);
    this->reject_control(reason);
    return false;
  }
  Request request{Command::INSTALLER_READ};
  request.parameter = static_cast<uint16_t>(parameter);
  request.token = ++this->token_;
  return this->engine_.enqueue(request, millis());
}

bool DuepiHub::installer_write(int parameter, int value) {
  if (!this->installer_parameter_allowed_(parameter) || value < 0 || value > 255 || !this->installer_write_ready_() ||
      this->installer_verifying_) {
    const char *reason = "Installer write rejected: range, busy, stale, unknown phase or ignition";
    if (this->text_[5] != nullptr) this->text_[5]->publish_state(reason);
    this->reject_control(reason);
    return false;
  }
  Request request{Command::INSTALLER_WRITE};
  request.parameter = static_cast<uint16_t>(parameter);
  request.argument = static_cast<uint16_t>(value);
  request.token = ++this->token_;
  if (!this->installer_permission_.reserve(request.token, millis())) {
    const char *reason = "Installer write rejected: disabled, not armed or expired";
    if (this->text_[5] != nullptr) this->text_[5]->publish_state(reason);
    this->reject_control(reason);
    return false;
  }
  return this->engine_.enqueue(request, millis());
}

bool DuepiHub::send_custom(const std::string &command, bool raw) {
  Request request;
  if (!custom_request(command, raw, request) || !this->installer_request_allowed_(request) ||
      (raw && !this->raw_enabled_) ||
      this->write_blocked_(Command::CUSTOM) || !this->privileged_ready_() || this->installer_verifying_ ||
      (request.payload[0] == 'S' && !this->installer_write_ready_())) {
    this->reject_control("Custom command rejected: format, parameter range, disabled, busy, stale, shutdown or OFF pending");
    return false;
  }
  request.token = ++this->token_;
  if (!this->custom_permission_.reserve(request.token, millis())) {
    this->reject_control("Custom command rejected: disabled, not armed or expired");
    return false;
  }
  this->custom_response_token_ = request.token;
  return this->engine_.enqueue(request, millis());
}

void DuepiHub::finish_installer_write_() {
  this->installer_verifying_ = false;
  this->verification_pending_ = false;
  this->state_.invalidate();
  this->sync_mask_ = 0;
  this->cycle_active_ = true;
  this->cursor_ = 0;
  this->publish_(millis());
}

void DuepiHub::restart_observation_() {
  this->state_.invalidate();
  this->engine_.resynchronize(millis());
  this->cycle_active_ = true;
  this->cursor_ = 0;
  this->publish_(millis());
}

bool DuepiHub::handle_privileged_result_(const Request &request, Outcome outcome, const Response *response) {
  bool installer = request.command == Command::INSTALLER_READ || request.command == Command::INSTALLER_WRITE;
  if (!installer && request.command != Command::CUSTOM) return false;
  static const char *const OUTCOMES[] = {"queued", "sent", "response received; unverified", "rejected",
                                        "timed out", "indeterminate; not retried", "cancelled", "verified"};
  char status[112];
  if (installer) {
    bool is_readback = request.command == Command::INSTALLER_READ && request.token != 0 &&
                    request.parameter == this->installer_expected_.parameter &&
                    request.token == this->installer_expected_.token;
    std::snprintf(status, sizeof(status), "PR%u: %s", unsigned(request.parameter), OUTCOMES[static_cast<size_t>(outcome)]);
    if (is_readback && (outcome == Outcome::REJECTED || outcome == Outcome::CANCELLED || outcome == Outcome::TIMED_OUT)) {
      this->finish_installer_write_();
      std::snprintf(status, sizeof(status), "PR%u write acknowledged; readback %s; write unverified",
                    unsigned(request.parameter), OUTCOMES[static_cast<size_t>(outcome)]);
      ESP_LOGW(TAG, "%s", status);
    }
    if (outcome == Outcome::ACKNOWLEDGED && response != nullptr) {
      if (request.command == Command::INSTALLER_READ && this->standby_switch_ != nullptr &&
          request.parameter == this->standby_parameter_) {
        if (response->byte() <= 1)
          this->standby_switch_->publish_observed(response->byte() == 1);
        else
          ESP_LOGW(TAG, "Standby parameter %u returned %u, expected 0 or 1; state not updated",
                   unsigned(request.parameter), unsigned(response->byte()));
      }
      if (request.command == Command::INSTALLER_WRITE) {
        this->installer_expected_ = request;
        this->installer_verifying_ = true;
        this->engine_.cancel_queued();
        Request readback{Command::INSTALLER_READ};
        readback.parameter = request.parameter;
        readback.token = request.token;
        readback.standby = request.standby;
        if (request.standby || this->installer_permission_.enabled()) {
          if (this->engine_.enqueue(readback, millis())) {
            std::snprintf(status, sizeof(status), "PR%u write acknowledged; awaiting readback", unsigned(request.parameter));
          } else {
            this->finish_installer_write_();
            std::snprintf(status, sizeof(status), "PR%u write acknowledged; readback rejected; write unverified", unsigned(request.parameter));
          }
        } else {
          this->finish_installer_write_();
          std::snprintf(status, sizeof(status), "PR%u write acknowledged; readback disabled, unverified", unsigned(request.parameter));
        }
      } else if (this->installer_verifying_ && request.parameter == this->installer_expected_.parameter &&
                 request.token == this->installer_expected_.token) {
        bool matches = response->byte() == this->installer_expected_.argument;
        if (!matches)
          ESP_LOGW(TAG, "Installer PR%u #%lu readback mismatch: requested %u, received %u; write unverified",
                   unsigned(request.parameter), static_cast<unsigned long>(request.token),
                   unsigned(this->installer_expected_.argument), unsigned(response->byte()));
        this->finish_installer_write_();
        std::snprintf(status, sizeof(status), "PR%u readback %u: %s", unsigned(request.parameter), unsigned(response->byte()),
                      matches ? "verified" : "MISMATCH, write unverified");
        this->restart_observation_();
      } else {
        std::snprintf(status, sizeof(status), "PR%u read value %u", unsigned(request.parameter), unsigned(response->byte()));
      }
    }
    if (outcome == Outcome::ACKNOWLEDGED) {
      if (request.standby && request.token == 0)
        ESP_LOGD(TAG, "Standby poll: %s", status);
      else
        ESP_LOGI(TAG, "Installer #%lu: %s", static_cast<unsigned long>(request.token), status);
    }
    if (!request.standby && this->text_[5] != nullptr) this->text_[5]->publish_state(status);
  } else {
    bool current = request.token == this->custom_response_token_;
    std::snprintf(status, sizeof(status), "Custom #%u: %s", unsigned(request.token), OUTCOMES[static_cast<size_t>(outcome)]);
    if (outcome == Outcome::SENT) {
      this->state_.invalidate();
      this->sync_mask_ = 0;
      this->engine_.cancel_queued();
      this->publish_(millis());
    }
    if (current && this->text_[6] != nullptr) {
      if (outcome == Outcome::QUEUED || outcome == Outcome::SENT)
        this->text_[6]->publish_state("Pending");
      else if (outcome == Outcome::REJECTED || outcome == Outcome::CANCELLED ||
               outcome == Outcome::TIMED_OUT || outcome == Outcome::INDETERMINATE)
        this->text_[6]->publish_state("Unavailable");
    }
    if (outcome == Outcome::ACKNOWLEDGED && response != nullptr) {
      char data[7];
      std::snprintf(data, sizeof(data), "%06lX", static_cast<unsigned long>(response->data));
      if (current && this->text_[6] != nullptr) this->text_[6]->publish_state(data);
      this->restart_observation_();
    }
    if (current && this->text_[4] != nullptr) this->text_[4]->publish_state(status);
  }
  return true;
}

}  // namespace duepi
}  // namespace esphome
#endif