#pragma once

#include "state.h"
#include "control.h"
#include "duepi_climate.h"
#ifdef USE_DUEPI_INSTALLER
#include "privilege.h"
#endif
#include "privileged.h"
#include "transaction.h"
#include "esphome/core/component.h"
#include "esphome/core/preferences.h"
#include "esphome/components/uart/uart.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/components/text_sensor/text_sensor.h"
#include "esphome/components/binary_sensor/binary_sensor.h"

namespace esphome {
namespace duepi {

class DuepiHub : public Component, public uart::UARTDevice, private TransactionIO {
 public:
  DuepiHub() : engine_(*this) {}
  void setup() override;
  void loop() override;
  void dump_config() override;
  void set_update_interval(uint32_t value) { this->update_interval_ = value; }
  void set_runtime_interval(uint32_t value) { this->runtime_interval_ = value; }
  void set_read_power_level(bool value) { this->read_power_level_ = value; }
  void set_response_timeout(uint32_t value) { this->engine_.timeout = value; }
  void set_inter_transaction_delay(uint32_t value) { this->engine_.spacing = value; }
  void set_quiet_time(uint32_t value) { this->engine_.quiet_time = value; }
  void set_stale_after(uint32_t value) { this->stale_after_ = value; }
  void set_numeric_sensor(size_t index, sensor::Sensor *entity) { this->numeric_[index] = entity; }
  void set_text_sensor(size_t index, text_sensor::TextSensor *entity) { this->text_[index] = entity; }
  void set_communication_sensor(binary_sensor::BinarySensor *entity) { this->communication_ = entity; }
  void set_enable_reset(bool value) { this->policy_.reset_enabled = value; }
  void set_min_temperature(uint16_t value) { this->policy_.min_temperature = value; }
  void set_max_temperature(uint16_t value) { this->policy_.max_temperature = value; }
  uint16_t min_temperature() const { return this->policy_.min_temperature; }
  uint16_t max_temperature() const { return this->policy_.max_temperature; }
  bool change_temperature_limit(bool minimum, float value);
  void set_temperature_limit_number(bool minimum, DuepiTemperatureLimit *entity) {
    this->temperature_limit_numbers_[minimum ? 0 : 1] = entity;
  }
  void set_init_command(bool value) { this->engine_.init_command = value; }
  bool init_command() const { return this->engine_.init_command; }
  void set_firmware_profile(FirmwareProfile value) { this->policy_.profile = value; }
  void set_climate(DuepiClimate *entity) { this->climate_ = entity; }
  void set_level_number(DuepiLevel *entity) { this->level_number_ = entity; }
  bool request_off();
  bool request_start();
  bool request_target(float value);
  bool request_level(uint16_t value);
  bool request_reset();
  void reject_control(const char *reason);
#ifdef USE_DUEPI_INSTALLER
  void set_enable_installer(bool value) { this->installer_permission_.configured = value; }
  void set_enable_custom_commands(bool value) { this->custom_permission_.configured = value; }
  void set_enable_raw_commands(bool value) { this->raw_enabled_ = value; }
  void set_installer_parameter_range(uint8_t minimum, uint8_t maximum) {
    this->installer_parameter_min_ = minimum;
    this->installer_parameter_max_ = maximum;
  }
  void set_arming_timeout(uint32_t value) {
    this->installer_permission_.lifetime = this->custom_permission_.lifetime = value;
    this->standby_permission_.lifetime = value;
  }
  void set_standby_mode(DuepiStandbySwitch *entity, uint8_t parameter) {
    this->standby_switch_ = entity;
    this->standby_parameter_ = parameter;
    this->standby_permission_.configured = true;
    this->standby_permission_.set_enabled(true);
  }
  bool request_standby(bool value);
  bool set_installer_access(bool value);
  bool set_installer_enabled(bool value);
  bool set_custom_enabled(bool value);
  bool installer_read(int parameter);
  bool request_installer_write(int parameter, int value);
  bool request_custom(const std::string &command, bool raw);
  void set_installer_number(bool value, DuepiInstallerValue *entity) { this->installer_numbers_[value ? 1 : 0] = entity; }
  bool installer_selected(bool write);
#endif
  void set_privilege_switch(size_t index, DuepiPrivilegeSwitch *entity) { this->privilege_switches_[index] = entity; }

 protected:
  void transmit(const WireFrame &frame) override;
  void result(const Request &request, Outcome outcome, const Response *response) override;
  bool may_transmit(const Request &request, uint32_t now) override;
  bool defer_off() const override { return this->sync_mask_ != 31; }
  void recovery_started() override;
  void diagnostic(const Request &request, const char *message) override;
#ifdef USE_DUEPI_INSTALLER
  bool arm_installer_write();
  bool arm_custom_command();
  bool installer_write(int parameter, int value);
  bool send_custom(const std::string &command, bool raw);
  bool handle_privileged_result_(const Request &request, Outcome outcome, const Response *response);
  bool privileged_ready_() const;
  bool installer_write_ready_() const;
  bool installer_parameter_allowed_(int parameter) const {
    return parameter >= this->installer_parameter_min_ && parameter <= this->installer_parameter_max_;
  }
  bool installer_request_allowed_(const Request &request) const;
  void finish_installer_write_();
  void restart_observation_();
#endif
  bool submit_(Request request);
  bool write_blocked_(Command command) const {
    if (command == Command::INSTALLER_WRITE) return false;
    return is_write(command) &&
        ((this->state_.fields[0].valid && ControlPolicy::shutdown_phase(this->state_.fields[0].data >> 8)) ||
         (command != Command::OFF && this->engine_.off_pending()));
  }
  void publish_(uint32_t now);
  void schedule_(uint32_t now);
  void publish_temperature_limits_();
  void publish_requested_level_(uint16_t level);
  void power_feedback_unavailable_();
  uint32_t lifetime_(size_t index) const;
  struct TemperatureLimits {
    uint16_t minimum;
    uint16_t maximum;
  };
  std::array<DuepiTemperatureLimit *, 2> temperature_limit_numbers_{};
  ESPPreferenceObject temperature_preferences_;
  bool temperature_preferences_ready_{false};
  TransactionEngine engine_;
  ObservedState state_;
  ControlPolicy policy_;
  DuepiClimate *climate_{nullptr};
  DuepiLevel *level_number_{nullptr};
  uint8_t sync_mask_{0};
  uint32_t token_{0};
  Request verification_{};
  bool verification_pending_{false};
  uint32_t verification_at_{0};
  float retained_target_{NAN};
  bool retained_target_requested_{false};
  bool read_power_level_{true};
  std::array<sensor::Sensor *, FIELD_COUNT> numeric_{};
  std::array<text_sensor::TextSensor *, 10> text_{};
  std::array<DuepiPrivilegeSwitch *, 3> privilege_switches_{};
#ifdef USE_DUEPI_INSTALLER
  OneShotPermission installer_permission_;
  OneShotPermission custom_permission_;
  OneShotPermission standby_permission_;
  DuepiStandbySwitch *standby_switch_{nullptr};
  uint8_t standby_parameter_{0};
  uint8_t installer_parameter_min_{0};
  uint8_t installer_parameter_max_{255};
  bool raw_enabled_{false};
  bool installer_verifying_{false};
  Request installer_expected_{};
  uint32_t custom_response_token_{0};
  std::array<DuepiInstallerValue *, 2> installer_numbers_{};
#endif
  binary_sensor::BinarySensor *communication_{nullptr};
  std::array<bool, FIELD_COUNT> published_fresh_{};
  bool published_health_{false};
  bool health_initialized_{false};
  bool read_timeout_logged_{false};
  uint32_t read_timeout_log_at_{0};
  uint32_t logged_parser_errors_{0};
  std::string last_rejection_reason_;
  uint32_t rejection_log_at_{0};
  bool rejection_logged_{false};
  bool cycle_active_{true};
  bool runtime_due_{true};
  uint8_t cursor_{0};
  uint32_t cycle_at_{0};
  uint32_t runtime_at_{0};
  uint32_t last_publish_{0};
  uint32_t update_interval_{10000};
  uint32_t runtime_interval_{3600000};
  uint32_t stale_after_{45000};
};

}  // namespace duepi
}  // namespace esphome