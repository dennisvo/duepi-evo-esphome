#pragma once

#include "state.h"
#include "esphome/components/climate/climate.h"
#include "esphome/components/number/number.h"
#include "esphome/components/button/button.h"

namespace esphome {
namespace duepi {

class DuepiHub;

class DuepiClimate : public climate::Climate {
 public:
  DuepiClimate();
  void set_parent(DuepiHub *parent) { this->parent_ = parent; }
  void publish_requested_level(uint16_t level);
  void publish_observed(const ObservedState &state, uint32_t now, uint32_t lifetime, float retained_target = NAN);
 protected:
  climate::ClimateTraits traits() override;
  void control(const climate::ClimateCall &call) override;
  DuepiHub *parent_{nullptr};
};

class DuepiLevel : public number::Number {
 public:
  void set_parent(DuepiHub *parent) { this->parent_ = parent; }
 protected:
  void control(float value) override;
  DuepiHub *parent_{nullptr};
};

class DuepiReset : public button::Button {
 public:
  void set_parent(DuepiHub *parent) { this->parent_ = parent; }
 protected:
  void press_action() override;
  DuepiHub *parent_{nullptr};
};

class DuepiTemperatureLimit : public number::Number {
 public:
  void set_parent(DuepiHub *parent) { this->parent_ = parent; }
  void set_minimum(bool value) { this->minimum_ = value; }
 protected:
  void control(float value) override;
  DuepiHub *parent_{nullptr};
  bool minimum_{false};
};

}  // namespace duepi
}  // namespace esphome