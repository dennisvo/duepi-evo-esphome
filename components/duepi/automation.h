#pragma once

#include "duepi.h"
#ifdef USE_DUEPI_INSTALLER
#include "esphome/core/automation.h"

namespace esphome {
namespace duepi {

enum class PrivilegedOperation : uint8_t { INSTALLER_READ, INSTALLER_WRITE, COMMAND, RAW };

template<typename... Ts> class PrivilegedAction : public Action<Ts...> {
 public:
  explicit PrivilegedAction(DuepiHub *parent) : parent_(parent) {}
  TEMPLATABLE_VALUE(int, parameter)
  TEMPLATABLE_VALUE(int, value)
  TEMPLATABLE_VALUE(std::string, command)
  void set_operation(PrivilegedOperation operation) { this->operation_ = operation; }
  void play(const Ts &...args) override {
    switch (this->operation_) {
      case PrivilegedOperation::INSTALLER_READ: this->parent_->installer_read(this->parameter_.value(args...)); break;
      case PrivilegedOperation::INSTALLER_WRITE:
        this->parent_->request_installer_write(this->parameter_.value(args...), this->value_.value(args...)); break;
      case PrivilegedOperation::COMMAND: this->parent_->request_custom(this->command_.value(args...), false); break;
      case PrivilegedOperation::RAW: this->parent_->request_custom(this->command_.value(args...), true); break;
    }
  }
 protected:
  DuepiHub *parent_;
  PrivilegedOperation operation_{PrivilegedOperation::INSTALLER_READ};
};

}  // namespace duepi
}  // namespace esphome
#endif