#include "duepi.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"
#include <cmath>
#include <cstdio>
#include <cstring>

namespace esphome {
namespace duepi {

static const char *const TAG = "duepi";

void DuepiHub::setup() {
  this->retained_target_ = NAN;
  this->retained_target_requested_ = false;
  this->publish_requested_level_(0);
#ifdef USE_DUEPI_INSTALLER
  this->installer_permission_.set_enabled(false);
  this->custom_permission_.set_enabled(false);
  for (size_t index = 0; index < this->installer_numbers_.size(); ++index)
    if (this->installer_numbers_[index] != nullptr) this->installer_numbers_[index]->publish_state(index == 0 ? 1 : 0);
#endif
  for (auto *entity : this->temperature_limit_numbers_) {
    if (entity == nullptr) continue;
    this->temperature_preferences_ = entity->make_entity_preference<TemperatureLimits>(0xD0E71001);
    this->temperature_preferences_ready_ = true;
    TemperatureLimits limits{};
    if (this->temperature_preferences_.load(&limits) && limits.minimum >= 10 &&
        limits.maximum <= 35 && limits.minimum < limits.maximum) {
      this->set_min_temperature(limits.minimum);
      this->set_max_temperature(limits.maximum);
    }
    break;
  }
  this->publish_temperature_limits_();
  ESP_LOGI(TAG, "Duepi startup: target limits %u..%u C; initialization %s",
           unsigned(this->min_temperature()),
           unsigned(this->max_temperature()), this->init_command() ? "enabled" : "disabled");
  this->cycle_at_ = this->runtime_at_ = millis();
  for (auto *entity : this->numeric_) if (entity != nullptr) entity->publish_state(NAN);
  for (auto *entity : this->text_) if (entity != nullptr) entity->publish_state("Unknown");
  for (size_t index = 0; index < this->privilege_switches_.size(); ++index) {
    auto *entity = this->privilege_switches_[index];
    if (entity != nullptr) entity->publish_state(false);
  }
  this->publish_(millis());
}

void DuepiHub::dump_config() {
  ESP_LOGCONFIG(TAG, "Duepi hub");
  ESP_LOGCONFIG(TAG, "Poll: %lu ms; stale: %lu ms", static_cast<unsigned long>(this->update_interval_),
                static_cast<unsigned long>(this->stale_after_));
  ESP_LOGCONFIG(TAG, "Response timeout: %lu ms; spacing: %lu ms; quiet: %lu ms",
                static_cast<unsigned long>(this->engine_.timeout), static_cast<unsigned long>(this->engine_.spacing),
                static_cast<unsigned long>(this->engine_.quiet_time));
  ESP_LOGCONFIG(TAG, "Target limits: %u..%u C; initialization: %s; firmware profile: %u",
                unsigned(this->min_temperature()), unsigned(this->max_temperature()),
                this->init_command() ? "enabled" : "disabled", unsigned(this->policy_.profile));
  ESP_LOGCONFIG(TAG, "Alarm reset: %s", this->policy_.reset_enabled ? "yes" : "no");
#ifdef USE_DUEPI_INSTALLER
  ESP_LOGCONFIG(TAG, "Configured opt-ins: reset=%s installer=%s custom=%s raw=%s",
                this->policy_.reset_enabled ? "yes" : "no", this->installer_permission_.configured ? "yes" : "no",
                this->custom_permission_.configured ? "yes" : "no", this->raw_enabled_ ? "yes" : "no");
#endif
}

void DuepiPrivilegeSwitch::write_state(bool value) {
#ifdef USE_DUEPI_INSTALLER
  this->publish_state(this->parent_->set_installer_access(value));
#else
  (void) value;
  this->publish_state(false);
#endif
}

void DuepiHub::recovery_started() {
  this->publish_requested_level_(0);
  this->power_feedback_unavailable_();
  ESP_LOGD(TAG, "UART resynchronization: control prerequisites cleared; one-shot permissions disarmed");
  this->sync_mask_ = 0;
  this->verification_pending_ = false;
#ifdef USE_DUEPI_INSTALLER
  this->installer_permission_.disarm();
  this->custom_permission_.disarm();
  if (this->installer_verifying_ && this->text_[5] != nullptr)
    this->text_[5]->publish_state("Installer write unverified: UART recovery");
  this->installer_verifying_ = false;
#endif
}

void DuepiHub::transmit(const WireFrame &frame) {
  ESP_LOGV(TAG, "TX frame: ESC %.8s &", reinterpret_cast<const char *>(frame.data() + 1));
  this->write_array(frame.data(), frame.size());
}

void DuepiHub::diagnostic(const Request &request, const char *message) {
  ESP_LOGD(TAG, "%s #%lu: %s", command_name(request.command), static_cast<unsigned long>(request.token), message);
}

bool DuepiHub::may_transmit(const Request &request, uint32_t now) {
  if (this->write_blocked_(request.command)) {
    this->diagnostic(request, "write blocked: ordinary shutdown active or OFF pending");
    return false;
  }
#ifdef USE_DUEPI_INSTALLER
  if (request.command == Command::INSTALLER_READ)
    return this->installer_permission_.enabled() && this->privileged_ready_();
  if (request.command == Command::INSTALLER_WRITE)
    return this->privileged_ready_() && this->state_.fields[0].data == 0 &&
           this->installer_permission_.consume(request.token, now);
  if (request.command == Command::CUSTOM)
    return this->privileged_ready_() && this->custom_permission_.consume(request.token, now);
#else
  if (is_installer(request.command)) return false;
#endif
  bool permitted = !is_write(request.command) ||
         (
#ifdef USE_DUEPI_INSTALLER
          !this->installer_verifying_ &&
#endif
          this->sync_mask_ == 31 && this->policy_.permits(request, this->state_, now, this->stale_after_));
  if (!permitted) {
    ESP_LOGD(TAG, "%s blocked: sync=0x%02X healthy=%u firmware_fresh=%u board_fresh=%u "
             "phase=0x%04lX alarm=%lu limits=%u..%u",
             command_name(request.command), unsigned(this->sync_mask_),
             unsigned(this->state_.healthy(now, this->stale_after_)),
             unsigned(this->state_.fresh(Command::FIRMWARE, now, this->stale_after_)),
             unsigned(this->state_.fresh(Command::BOARD, now, this->stale_after_)),
             static_cast<unsigned long>(this->state_.fields[0].data >> 8),
             static_cast<unsigned long>(this->state_.fields[6].data >> 8),
             unsigned(this->min_temperature()), unsigned(this->max_temperature()));
  }
  return permitted;
}

void DuepiHub::reject_control(const char *reason) {
  ESP_LOGD(TAG, "Control rejected: %s", reason);
  if (!this->rejection_logged_ || this->last_rejection_reason_ != reason ||
      uint32_t(millis() - this->rejection_log_at_) >= 30000) {
    ESP_LOGW(TAG, "%s (identical rejection warnings limited to 30s)", reason);
    this->last_rejection_reason_ = reason;
    this->rejection_log_at_ = millis();
    this->rejection_logged_ = true;
  }
  if (this->text_[4] != nullptr) this->text_[4]->publish_state(reason);
}

bool DuepiHub::change_temperature_limit(bool minimum, float value) {
  if (!std::isfinite(value) || std::floor(value) != value || value < 10 || value > 35 ||
      (minimum ? value >= this->max_temperature() : value <= this->min_temperature())) {
    this->reject_control("Rejected: limits require whole Celsius 10..35 and minimum below maximum");
    this->publish_temperature_limits_();
    return false;
  }
  if (value == (minimum ? this->min_temperature() : this->max_temperature())) {
    this->publish_temperature_limits_();
    return true;
  }
  if (minimum) this->set_min_temperature(static_cast<uint16_t>(value));
  else this->set_max_temperature(static_cast<uint16_t>(value));
  ESP_LOGI(TAG, "Target command limits changed to %u..%u C; no stove command sent",
           unsigned(this->min_temperature()), unsigned(this->max_temperature()));
  if (this->temperature_preferences_ready_) {
    TemperatureLimits limits{this->min_temperature(), this->max_temperature()};
    if (!this->temperature_preferences_.save(&limits))
      ESP_LOGW(TAG, "Could not save temperature limits");
  }
  this->publish_temperature_limits_();
  return true;
}

void DuepiHub::publish_temperature_limits_() {
  if (this->temperature_limit_numbers_[0] != nullptr)
    this->temperature_limit_numbers_[0]->publish_state(this->min_temperature());
  if (this->temperature_limit_numbers_[1] != nullptr)
    this->temperature_limit_numbers_[1]->publish_state(this->max_temperature());
}

bool DuepiHub::submit_(Request request) {
  uint32_t now = millis();
  if (this->write_blocked_(request.command)) {
    this->reject_control("Rejected: ordinary shutdown active or OFF pending");
    return false;
  }
  if (!this->may_transmit(request, now) || this->engine_.recovering()) {
    this->reject_control("Rejected: disabled, unsynchronized, stale, alarm or firmware/phase restriction");
    return false;
  }
  request.token = ++this->token_;
  return this->engine_.enqueue(request, now);
}

bool DuepiHub::request_off() { return this->submit_({Command::OFF}); }
bool DuepiHub::request_start() {
  Request request;
  this->policy_.start_request(this->state_, request, this->desired_level_);
  return this->submit_(request);
}
bool DuepiHub::request_target(float value) {
  Request request;
  if (!ControlPolicy::target_request(value, request)) {
    this->reject_control("Rejected: target must be whole Celsius within the supported range");
    return false;
  }
  return this->submit_(request);
}
bool DuepiHub::request_level(uint16_t value) {
  if (value < 1 || value > 5) {
    this->reject_control("Rejected: requested power must be 1..5");
    return false;
  }
  if (this->state_.fresh(Command::PHASE, millis(), this->stale_after_) && this->state_.fields[0].data == 0) {
    Request start;
    this->policy_.start_request(this->state_, start, value);
    if (this->engine_.recovering() || !this->may_transmit(start, millis())) {
      this->reject_control("Rejected: local power selection requires fresh, synchronized, alarm-free Off state");
      return false;
    }
    this->desired_level_ = value;
    ++this->token_;
    this->local_level_ = true;
    this->has_level_ = true;
    this->verification_pending_ = false;
    char buffer[96];
    std::snprintf(buffer, sizeof(buffer), "Power %u selected locally; no stove command sent", unsigned(value));
    if (this->text_[4] != nullptr) this->text_[4]->publish_state(buffer);
    this->publish_requested_level_(value);
    this->publish_(millis());
    return true;
  }
  return this->submit_({Command::SET_LEVEL, value});
}
bool DuepiHub::request_reset() { return this->submit_({Command::RESET}); }

uint32_t DuepiHub::lifetime_(size_t index) const {
  return index == 12 || index == 13 ? this->runtime_interval_ * 2 : this->stale_after_;
}

void DuepiHub::power_feedback_unavailable_() {
  if (!this->verification_pending_ || this->verification_.command != Command::SET_LEVEL ||
      this->verification_.token != this->token_) return;
  char buffer[112];
  std::snprintf(buffer, sizeof(buffer), "set_level #%u requested=%u: readback unavailable (unverified)",
                unsigned(this->verification_.token), unsigned(this->verification_.argument));
  if (this->text_[4] != nullptr) this->text_[4]->publish_state(buffer);
  this->verification_pending_ = false;
}

void DuepiHub::result(const Request &request, Outcome outcome, const Response *response) {
  static const char *const OUTCOMES[] = {"queued", "sent", "acknowledged, unverified", "rejected",
                                        "timed out", "indeterminate: not retried", "cancelled", "verified by readback"};
  const char *description = OUTCOMES[static_cast<size_t>(outcome)];
  if (outcome == Outcome::ACKNOWLEDGED && response != nullptr)
    ESP_LOGV(TAG, "RX %s #%lu: accepted payload=%06lX", command_name(request.command),
             static_cast<unsigned long>(request.token), static_cast<unsigned long>(response->data));
  ESP_LOGD(TAG, "%s #%lu: %s (argument=%u, parameter=%u)", command_name(request.command),
           static_cast<unsigned long>(request.token), description, unsigned(request.argument), unsigned(request.parameter));
  if (is_write(request.command)) {
    if (outcome == Outcome::REJECTED || outcome == Outcome::INDETERMINATE || outcome == Outcome::TIMED_OUT) {
      ESP_LOGW(TAG, "%s #%lu: %s", command_name(request.command), static_cast<unsigned long>(request.token), description);
    } else if (outcome == Outcome::SENT || outcome == Outcome::ACKNOWLEDGED ||
           outcome == Outcome::VERIFIED || outcome == Outcome::CANCELLED) {
      ESP_LOGI(TAG, "%s #%lu: %s (argument=%u, parameter=%u)", command_name(request.command),
               static_cast<unsigned long>(request.token), description, unsigned(request.argument), unsigned(request.parameter));
    }
  } else if (outcome == Outcome::TIMED_OUT &&
             (!this->read_timeout_logged_ || uint32_t(millis() - this->read_timeout_log_at_) >= 30000)) {
    ESP_LOGW(TAG, "No response to %s; read attempts exhausted (warnings limited to 30s)",
             command_name(request.command));
    this->read_timeout_logged_ = true;
    this->read_timeout_log_at_ = millis();
  }
  if (outcome == Outcome::ACKNOWLEDGED && response != nullptr && this->text_[7] != nullptr) {
    char timestamp[48];
    const unsigned long seconds = millis() / 1000;
    std::snprintf(timestamp, sizeof(timestamp), "%lud %02luh %02lum %02lus",
                  seconds / 86400, (seconds / 3600) % 24, (seconds / 60) % 60, seconds % 60);
    this->text_[7]->publish_state(timestamp);
  }
#ifdef USE_DUEPI_INSTALLER
  if (this->handle_privileged_result_(request, outcome, response)) return;
#endif
  auto index = static_cast<size_t>(request.command);
  if (outcome == Outcome::ACKNOWLEDGED && response != nullptr && index < FIELD_COUNT) {
    bool changed = !this->state_.fields[index].valid || this->state_.fields[index].data != response->data;
    if (request.command == Command::FIRMWARE && response->payload[0] != 0)
      changed = changed || std::strncmp(this->state_.firmware.data(), response->payload.data(), 4) != 0;
    if (changed && request.command == Command::PHASE)
      ESP_LOGI(TAG, "Stove phase: %s (0x%04X)", phase_name(response->word()), unsigned(response->word()));
    if (changed && request.command == Command::ALARM) {
      if (response->word() != 0) ESP_LOGW(TAG, "Stove reports alarm code %u", unsigned(response->word()));
      else ESP_LOGI(TAG, "Stove reports no alarm");
    }
    if (changed && request.command == Command::BOARD)
      ESP_LOGI(TAG, "Controller board code: %u (type mapping unverified)", unsigned(response->byte()));
    if (!this->state_.update(request.command, *response, millis())) {
      if (request.command == Command::TARGET || request.command == Command::REQUESTED_POWER) {
        ESP_LOGD(TAG, "%s feedback unavailable: 0x%04X",
                 request.command == Command::TARGET ? "Target" : "Power level", unsigned(response->word()));
        if (this->numeric_[index] != nullptr) this->numeric_[index]->publish_state(NAN);
        this->published_fresh_[index] = false;
        if (request.command == Command::REQUESTED_POWER) this->power_feedback_unavailable_();
      }
      return;
    }
    if (changed && request.command == Command::FIRMWARE)
      ESP_LOGI(TAG, "Controller firmware: %s", this->state_.firmware.data());
    if (request.command == Command::PHASE && response->word() != 0)
      this->local_level_ = false;
    if (request.command == Command::PHASE && ControlPolicy::shutdown_phase(response->word())) {
      ESP_LOGD(TAG, "Ordinary shutdown observed: cancelling queued writes; reads remain available");
      this->engine_.cancel_writes();
    }
    if (request.command == Command::TARGET) {
      this->retained_target_ = response->word();
      this->retained_target_requested_ = false;
    }
    if (this->verification_pending_ && uint32_t(millis() - this->verification_at_) >= 15000) {
      this->power_feedback_unavailable_();
      this->verification_pending_ = false;
    }
    if (request.command == Command::REQUESTED_POWER && !this->local_level_) {
      this->desired_level_ = response->word();
      this->has_level_ = true;
      if (this->verification_pending_ && this->verification_.command == Command::SET_LEVEL &&
          this->verification_.token == this->token_ && response->word() != this->verification_.argument) {
        char buffer[112];
        std::snprintf(buffer, sizeof(buffer), "set_level #%u requested=%u reported=%u: readback mismatch",
                      unsigned(this->verification_.token), unsigned(this->verification_.argument), unsigned(response->word()));
        if (this->text_[4] != nullptr) this->text_[4]->publish_state(buffer);
        this->verification_pending_ = false;
      }
    }
    if (this->verification_pending_ && this->verification_.token == this->token_ &&
        ControlPolicy::verified_by(this->verification_, request.command, *response)) {
      this->verification_pending_ = false;
      this->result(this->verification_, Outcome::VERIFIED, nullptr);
    }
    if (request.command == Command::FIRMWARE) this->sync_mask_ |= 1;
    if (request.command == Command::BOARD) this->sync_mask_ |= 2;
    if (request.command == Command::PHASE) this->sync_mask_ |= 4;
    if (request.command == Command::ROOM) this->sync_mask_ |= 8;
    if (request.command == Command::ALARM) this->sync_mask_ |= 16;
    if (this->numeric_[index] != nullptr)
      this->numeric_[index]->publish_state(numeric_value(request.command, response->data));
    this->published_fresh_[index] = true;
    char buffer[48];
    if (request.command == Command::CYCLE_TIME && this->text_[9] != nullptr) {
      std::snprintf(buffer, sizeof(buffer), "%02u:%02u", unsigned(response->byte()),
                    unsigned((response->data >> 8) & 0xFF));
      this->text_[9]->publish_state(buffer);
    }
    if (request.command == Command::PHASE && this->text_[0] != nullptr) {
      std::snprintf(buffer, sizeof(buffer), "%s (0x%04X)", phase_name(response->word()), response->word());
      this->text_[0]->publish_state(buffer);
    }
    if (request.command == Command::FIRMWARE && this->text_[1] != nullptr) {
      this->text_[1]->publish_state(this->state_.firmware.data());
    }
    if (request.command == Command::BOARD && this->text_[2] != nullptr)
      this->text_[2]->publish_state(response->byte() == 0 ? "Air" : "Hydro");
    if ((request.command == Command::ALARM || request.command == Command::FIRMWARE) && this->text_[3] != nullptr &&
        this->state_.fresh(Command::ALARM, millis(), this->stale_after_)) {
      uint16_t code = this->state_.fields[static_cast<size_t>(Command::ALARM)].data >> 8;
      const char *description = alarm_name(code, std::strcmp(this->state_.firmware.data(), "RE8I") == 0);
      std::snprintf(buffer, sizeof(buffer), "%u: %s", unsigned(code), description);
      this->text_[3]->publish_state(buffer);
    }
  }
  if (is_write(request.command)) {
    char buffer[96];
    std::snprintf(buffer, sizeof(buffer), "%s #%u value=%u: %s", command_name(request.command), unsigned(request.token),
            unsigned(request.argument), OUTCOMES[static_cast<size_t>(outcome)]);
    if (this->text_[4] != nullptr && request.token == this->token_) this->text_[4]->publish_state(buffer);
    if (outcome == Outcome::ACKNOWLEDGED) {
      if (request.command == Command::SET_LEVEL && request.token == this->token_) {
        this->desired_level_ = request.argument;
        this->local_level_ = false;
        this->has_level_ = true;
        this->publish_requested_level_(request.argument);
      }
        if ((request.command == Command::START_GENERIC || request.command == Command::START_9200 ||
           request.command == Command::START_E670 || request.command == Command::START_E693) &&
          request.token == this->token_) {
        this->desired_level_ = request.argument;
        this->local_level_ = false;
        this->has_level_ = true;
      }
      if (request.command == Command::SET_TARGET) {
        this->retained_target_ = request.argument;
        this->retained_target_requested_ = true;
      }
      this->verification_ = request;
      this->verification_pending_ = true;
      this->verification_at_ = millis();
      if (request.command == Command::OFF || request.command == Command::RESET ||
          request.command == Command::START_9200 || request.command == Command::START_E670 ||
          request.command == Command::START_E693 || request.command == Command::START_GENERIC) this->sync_mask_ = 0;
      this->cycle_active_ = true;
      this->cursor_ = 0;
    }
  }
  if (this->token_ == 0 && this->text_[4] != nullptr && (outcome == Outcome::ACKNOWLEDGED || outcome == Outcome::TIMED_OUT)) {
    char buffer[64];
    std::snprintf(buffer, sizeof(buffer), "Read %u: %s", unsigned(request.command),
                  outcome == Outcome::ACKNOWLEDGED ? "response accepted" : "timed out");
    this->text_[4]->publish_state(buffer);
  }
}

void DuepiHub::publish_requested_level_(uint16_t level) {
  if (this->climate_ != nullptr) this->climate_->publish_requested_level(level);
  float value = level >= 1 && level <= 5 ? static_cast<float>(level) : NAN;
  if (this->level_number_ != nullptr &&
      (std::isfinite(value) ? this->level_number_->state != value : std::isfinite(this->level_number_->state)))
    this->level_number_->publish_state(value);
}

void DuepiHub::publish_(uint32_t now) {
  uint16_t phase = static_cast<uint16_t>(this->state_.fields[0].data >> 8);
  if (this->verification_pending_ && uint32_t(now - this->verification_at_) >= 15000)
    this->power_feedback_unavailable_();
  if (this->sync_mask_ != 31 || !this->state_.healthy(now, this->stale_after_) || ControlPolicy::shutdown_phase(phase))
    this->publish_requested_level_(0);
  else
    this->publish_requested_level_(this->has_level_ ? this->desired_level_ : 0);
  if (this->climate_ != nullptr)
    this->climate_->publish_observed(this->state_, now, this->stale_after_, this->retained_target_);
  if (this->text_[8] != nullptr) {
    const char *source = this->state_.fresh(Command::TARGET, now, this->stale_after_) ? "Reported" :
                         !std::isfinite(this->retained_target_) ? "Unknown" :
                         this->retained_target_requested_ ? "Acknowledged request (unverified)" : "Retained reading (unverified)";
    if (this->text_[8]->state != source) this->text_[8]->publish_state(source);
  }
  bool healthy = this->state_.healthy(now, this->stale_after_);
  if (!this->health_initialized_ || healthy != this->published_health_) {
    if (healthy) {
      ESP_LOGI(TAG, "Communication healthy: phase, room and alarm are fresh");
    } else if (this->health_initialized_) {
      ESP_LOGW(TAG, "Communication lost: core readings are stale or unavailable");
    } else {
      ESP_LOGI(TAG, "Waiting for fresh phase, room and alarm readings");
    }
    if (this->communication_ != nullptr) this->communication_->publish_state(healthy);
    this->published_health_ = healthy;
    this->health_initialized_ = true;
  }
  for (size_t index = 0; index < FIELD_COUNT; ++index) {
    if (this->published_fresh_[index] && !this->state_.fields[index].fresh(now, this->lifetime_(index))) {
      this->published_fresh_[index] = false;
      if (this->numeric_[index] != nullptr) this->numeric_[index]->publish_state(NAN);
      int text_index = index == 0 ? 0 : index == 10 ? 1 : index == 11 ? 2 : index == 6 ? 3 :
               index == static_cast<size_t>(Command::CYCLE_TIME) ? 9 : -1;
      if (text_index >= 0 && this->text_[text_index] != nullptr) this->text_[text_index]->publish_state("Stale");
    }
  }
}

void DuepiHub::schedule_(uint32_t now) {
  static const Command ORDER[] = {
      Command::FIRMWARE, Command::BOARD, Command::PHASE, Command::ROOM, Command::ALARM,
      Command::REQUESTED_POWER, Command::TARGET, Command::D3000, Command::D4000, Command::FLUE, Command::EXHAUST,
      Command::INPUTS, Command::PCB,
      Command::FLUE_EXTRACTION_SPEED, Command::EXTRACTION_SENSOR_LEVEL,
      Command::FLUE_MOTOR_CORRECTION, Command::PELLET_CORRECTION, Command::CYCLE_TIME,
      Command::TOTAL_RUNTIME, Command::SERVICE_RUNTIME};
  if (!this->cycle_active_ && uint32_t(now - this->cycle_at_) >= this->update_interval_) {
    this->cycle_active_ = true;
    this->cycle_at_ = now;
    this->cursor_ = 0;
  }
  if (uint32_t(now - this->runtime_at_) >= this->runtime_interval_) this->runtime_due_ = true;
  if (!this->cycle_active_ || !this->engine_.ready_to_poll()) return;
  size_t count = sizeof(ORDER) / sizeof(ORDER[0]) - (this->runtime_due_ ? 0 : 2);
  while (this->cursor_ < count) {
    Command command = ORDER[this->cursor_++];
    if (command == Command::REQUESTED_POWER && !this->read_power_level_) continue;
    if (command >= Command::FLUE_EXTRACTION_SPEED && command <= Command::CYCLE_TIME &&
        (command == Command::CYCLE_TIME ? this->text_[9] == nullptr :
         this->numeric_[static_cast<size_t>(command)] == nullptr)) continue;
    this->engine_.enqueue({command}, now);
    return;
  }
  this->cycle_active_ = false;
  if (this->runtime_due_) this->runtime_at_ = now;
  this->runtime_due_ = false;
}

void DuepiHub::loop() {
  uint32_t now = millis();
  uint8_t byte = 0;
  for (size_t budget = 0; budget < 64 && this->available() != 0; ++budget) {
    if (!this->read_byte(&byte)) break;
    this->engine_.receive(byte, now);
  }
  if (this->available() == 0) {
    this->schedule_(now);
    this->engine_.tick(now);
  }
  if (uint32_t(now - this->last_publish_) >= 1000) {
    uint32_t errors = this->engine_.parser_errors();
    if (errors != this->logged_parser_errors_) {
      ESP_LOGD(TAG, "Parser rejected/expired frames: +%lu, total %lu",
               static_cast<unsigned long>(errors - this->logged_parser_errors_), static_cast<unsigned long>(errors));
      this->logged_parser_errors_ = errors;
    }
    this->last_publish_ = now;
    this->publish_(now);
  }
}

}  // namespace duepi
}  // namespace esphome