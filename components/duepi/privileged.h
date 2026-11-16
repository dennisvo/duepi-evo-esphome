#pragma once

#include "esphome/components/switch/switch.h"
#ifdef USE_DUEPI_INSTALLER
#include "esphome/core/component.h"
#include "esphome/components/number/number.h"
#include "esphome/components/button/button.h"
#ifdef USE_API
#include "esphome/components/api/custom_api_device.h"
#endif
#endif

namespace esphome {
namespace duepi {

class DuepiHub;

class DuepiPrivilegeSwitch : public switch_::Switch {
 public:
  void set_parent(DuepiHub *parent) { this->parent_ = parent; }
  void set_initialization(bool value) { this->initialization_ = value; }
 protected:
  void write_state(bool value) override;
  DuepiHub *parent_{nullptr};
  bool initialization_{false};
};

#ifdef USE_DUEPI_INSTALLER
class DuepiInstallerValue : public number::Number {
 protected:
  void control(float value) override;
};

class DuepiInstallerButton : public button::Button {
 public:
  void set_parent(DuepiHub *parent) { this->parent_ = parent; }
  void set_operation(uint8_t operation) { this->operation_ = operation; }
 protected:
  void press_action() override;
  DuepiHub *parent_{nullptr};
  uint8_t operation_{0};
};

#ifdef USE_API
class DuepiInstallerAPI : public Component, public api::CustomAPIDevice {
 public:
  void set_parent(DuepiHub *parent) { this->parent_ = parent; }
  void set_action_prefix(const std::string &prefix) { this->prefix_ = prefix; }
  void setup() override;
  void read(int32_t parameter);
  void write(int32_t parameter, int32_t value);
  void command(std::string command);
  void raw(std::string payload);
 protected:
  DuepiHub *parent_{nullptr};
  std::string prefix_;
};
#endif
#endif

}  // namespace duepi
}  // namespace esphome