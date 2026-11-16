#pragma once
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdarg>
#include <cstdio>
#include <cstring>
#include <deque>
#include <map>
#include <optional>
#include <string>
#include <vector>

#define ESP_LOGCONFIG(...) ::esphome::test_log("CONFIG", __VA_ARGS__)
#define ESP_LOGW(...) ::esphome::test_log("WARN", __VA_ARGS__)
#define ESP_LOGI(...) ::esphome::test_log("INFO", __VA_ARGS__)
#define ESP_LOGD(...) ::esphome::test_log("DEBUG", __VA_ARGS__)
#define ESP_LOGV(...) ::esphome::test_log("VERBOSE", __VA_ARGS__)

namespace esphome {
struct TestLog {
  std::string level;
  std::string tag;
  std::string message;
};
inline std::vector<TestLog> test_logs;
inline int test_log_level = 6;
inline void test_log(const char *level, const char *tag, const char *format, ...)
    __attribute__((format(printf, 3, 4)));
inline void test_log(const char *level, const char *tag, const char *format, ...) {
  if ((std::strcmp(level, "VERBOSE") == 0 && test_log_level < 6) ||
      ((std::strcmp(level, "DEBUG") == 0 || std::strcmp(level, "CONFIG") == 0) && test_log_level < 5)) return;
  char message[512];
  va_list arguments;
  va_start(arguments, format);
  std::vsnprintf(message, sizeof(message), format, arguments);
  va_end(arguments);
  test_logs.push_back({level, tag, message});
}
inline std::map<uint32_t, std::vector<uint8_t>> test_preferences;
class ESPPreferenceObject {
 public:
  uint32_t key{0};
  template<typename Value> bool save(const Value *value) {
    auto *bytes = reinterpret_cast<const uint8_t *>(value);
    test_preferences[this->key] = std::vector<uint8_t>(bytes, bytes + sizeof(Value));
    return true;
  }
  template<typename Value> bool load(Value *value) {
    auto found = test_preferences.find(this->key);
    if (found == test_preferences.end() || found->second.size() != sizeof(Value)) return false;
    std::memcpy(value, found->second.data(), sizeof(Value));
    return true;
  }
};
inline uint32_t test_now = 0;
inline uint32_t millis() { return test_now; }
class Component {
 public:
  virtual ~Component() = default;
  virtual void setup() {}
  virtual void loop() {}
  virtual void dump_config() {}
};
namespace uart {
class UARTDevice {
 public:
  std::deque<uint8_t> input;
  std::vector<std::vector<uint8_t>> writes;
  size_t available() const { return input.size(); }
  bool read_byte(uint8_t *byte) {
    if (input.empty()) return false;
    *byte = input.front();
    input.pop_front();
    return true;
  }
  void write_array(const uint8_t *data, size_t size) { writes.emplace_back(data, data + size); }
};
}
namespace sensor {
class Sensor {
 public:
  float state{NAN};
  void publish_state(float value) { state = value; }
};
}
namespace text_sensor {
class TextSensor {
 public:
  std::string state;
  void publish_state(const char *value) { state = value; }
};
}
namespace binary_sensor {
class BinarySensor {
 public:
  bool state{false};
  void publish_state(bool value) { state = value; }
};
}
namespace climate {
enum ClimateMode { CLIMATE_MODE_OFF, CLIMATE_MODE_HEAT };
enum ClimateAction { CLIMATE_ACTION_OFF, CLIMATE_ACTION_IDLE, CLIMATE_ACTION_HEATING };
constexpr uint32_t CLIMATE_SUPPORTS_CURRENT_TEMPERATURE = 1;
constexpr uint32_t CLIMATE_SUPPORTS_ACTION = 2;
class ClimateTraits {
 public:
  void set_supported_modes(std::initializer_list<ClimateMode>) {}
  void add_feature_flags(uint32_t) {}
  void set_visual_min_temperature(float) {}
  void set_visual_max_temperature(float) {}
  void set_visual_temperature_step(float) {}
};
class ClimateCall {
 public:
  std::optional<ClimateMode> mode;
  std::optional<float> target;
  const std::optional<ClimateMode> &get_mode() const { return mode; }
  const std::optional<float> &get_target_temperature() const { return target; }
};
class Climate {
 public:
  virtual ~Climate() = default;
  float current_temperature{NAN};
  float target_temperature{NAN};
  ClimateMode mode{CLIMATE_MODE_OFF};
  ClimateAction action{CLIMATE_ACTION_OFF};
  size_t publications{0};
  void publish_state() { ++publications; }
 protected:
  virtual ClimateTraits traits() = 0;
  virtual void control(const ClimateCall &) = 0;
};
}
namespace number {
class Number {
 public:
  virtual ~Number() = default;
  uint32_t preference_key{1};
  template<typename Value> ESPPreferenceObject make_entity_preference(uint32_t version = 0) {
    return {this->preference_key ^ version};
  }
  float state{NAN};
  void publish_state(float value) { state = value; }
 protected:
  virtual void control(float) = 0;
};
}
namespace button {
class Button {
 public:
  virtual ~Button() = default;
 protected:
  virtual void press_action() = 0;
};
}
namespace switch_ {
class Switch {
 public:
  virtual ~Switch() = default;
  bool state{false};
  void publish_state(bool value) { state = value; }
 protected:
  virtual void write_state(bool) = 0;
};
}
}