#include "duepi_climate.h"
#include "duepi.h"
#include <cmath>

namespace esphome {
namespace duepi {

static const char *const POWER_PRESETS[] = {
    "Power level 1", "Power level 2", "Power level 3", "Power level 4", "Power level 5"};

DuepiClimate::DuepiClimate() { this->set_supported_custom_presets(POWER_PRESETS); }

void DuepiClimate::publish_requested_level(uint16_t level) {
  if (level >= 1 && level <= 5) {
    if (this->set_custom_preset_(POWER_PRESETS[level - 1])) this->publish_state();
  } else if (this->has_custom_preset()) {
    this->clear_custom_preset_();
    this->publish_state();
  }
}

climate::ClimateTraits DuepiClimate::traits() {
  climate::ClimateTraits traits;
  traits.set_supported_modes({climate::CLIMATE_MODE_OFF, climate::CLIMATE_MODE_HEAT});
  traits.add_feature_flags(climate::CLIMATE_SUPPORTS_CURRENT_TEMPERATURE | climate::CLIMATE_SUPPORTS_ACTION);
  traits.set_visual_min_temperature(this->parent_->min_temperature());
  traits.set_visual_max_temperature(this->parent_->max_temperature());
  traits.set_visual_temperature_step(1);
  return traits;
}

void DuepiClimate::control(const climate::ClimateCall &call) {
  if (call.get_mode().has_value() && *call.get_mode() == climate::CLIMATE_MODE_OFF) {
    this->parent_->request_off();
    return;
  }
  if (call.has_custom_preset()) {
    uint16_t level = 1;
    while (level <= 5 && call.get_custom_preset() != POWER_PRESETS[level - 1]) ++level;
    if (level > 5) {
      this->parent_->reject_control("Invalid requested power preset");
      return;
    }
    this->parent_->request_level(level);
  }
  if (call.get_target_temperature().has_value()) this->parent_->request_target(*call.get_target_temperature());
  if (call.get_mode().has_value() && *call.get_mode() == climate::CLIMATE_MODE_HEAT) this->parent_->request_start();
}

void DuepiClimate::publish_observed(const ObservedState &state, uint32_t now, uint32_t lifetime, float retained_target) {
  using namespace climate;
  float room = state.fresh(Command::ROOM, now, lifetime) ? numeric_value(Command::ROOM, state.fields[1].data) : NAN;
  float target = state.fresh(Command::TARGET, now, lifetime) ? numeric_value(Command::TARGET, state.fields[8].data) : retained_target;
  auto mode = this->mode;
  auto action = this->action;
  if (state.fresh(Command::PHASE, now, lifetime)) {
    uint16_t phase = static_cast<uint16_t>(state.fields[0].data >> 8);
    if (known_phase(phase)) {
      mode = CLIMATE_MODE_OFF;
      action = CLIMATE_ACTION_OFF;
      if (phase != 0 && phase != 0x0301 && phase != 0x0801 && phase != 0x0802) {
        mode = CLIMATE_MODE_HEAT;
        action = phase >= 0x1000 ? CLIMATE_ACTION_IDLE : CLIMATE_ACTION_HEATING;
      }
    }
  }
  bool same_room = room == this->current_temperature || (std::isnan(room) && std::isnan(this->current_temperature));
  bool same_target = target == this->target_temperature || (std::isnan(target) && std::isnan(this->target_temperature));
  if (same_room && same_target && mode == this->mode && action == this->action) return;
  this->current_temperature = room;
  this->target_temperature = target;
  this->mode = mode;
  this->action = action;
  this->publish_state();
}

void DuepiLevel::control(float value) {
  if (std::isfinite(value) && value >= 1 && value <= 5 && std::floor(value) == value) {
    this->parent_->request_level(static_cast<uint16_t>(value));
  } else {
    this->parent_->reject_control("Invalid requested level");
  }
}

void DuepiReset::press_action() { this->parent_->request_reset(); }

void DuepiTemperatureLimit::control(float value) {
  this->parent_->change_temperature_limit(this->minimum_, value);
}

}  // namespace duepi
}  // namespace esphome