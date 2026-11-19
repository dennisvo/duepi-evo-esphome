#include "duepi.h"
#include <cstdlib>
#include <iostream>

using namespace esphome;
using namespace esphome::duepi;

#define CHECK(condition) do { if (!(condition)) { std::cerr << __LINE__ << ": " << #condition << '\n'; std::exit(1); } } while (false)

class TestHub : public DuepiHub {
 public:
#ifdef USE_DUEPI_INSTALLER
  using DuepiHub::arm_installer_write;
  using DuepiHub::arm_custom_command;
  using DuepiHub::installer_write;
  using DuepiHub::send_custom;
#endif
  void observe(Command command, uint32_t data) {
    Response response{data};
    this->result({command}, Outcome::ACKNOWLEDGED, &response);
    this->publish_(millis());
  }
  void synchronize() {
    this->observe(Command::FIRMWARE, 0x920000);
    this->observe(Command::BOARD, 0);
    this->observe(Command::PHASE, 0);
    this->observe(Command::ROOM, 0x00D700);
    this->observe(Command::ALARM, 0);
  }
  void tick() { this->engine_.tick(millis()); }
  void poll() { this->schedule_(millis()); this->tick(); }
  bool queue(Request request) { return this->engine_.enqueue(request, millis()); }
  size_t queued() const { return this->engine_.queued(); }
  void cache_phase(uint16_t phase) {
    CHECK(this->state_.update(Command::PHASE, Response{static_cast<uint32_t>(phase) << 8}, millis()));
  }
  void read_target() {
    CHECK(this->engine_.enqueue({Command::TARGET}, millis()));
    this->tick();
  }
  void receive(const char *data) {
    WireFrame frame{};
    CHECK(encode_payload(data, 6, frame));
    for (auto byte : frame) this->engine_.receive(byte, millis());
  }
  void publish() { this->publish_(millis()); }
  void recover() { this->engine_.resynchronize(millis()); }
  void report(const Request &request, Outcome outcome) { this->result(request, outcome, nullptr); }
};

#ifdef USE_DUEPI_INSTALLER
class TestInstallerValue : public DuepiInstallerValue {
 public:
  void change(float value) { this->control(value); }
};

class TestInstallerSwitch : public DuepiPrivilegeSwitch {
 public:
  void change(bool value) { this->write_state(value); }
};

class TestStandbySwitch : public DuepiStandbySwitch {
 public:
  void change(bool value) { this->write_state(value); }
  bool known() const { return this->known_; }
};
#endif

class TestClimate : public DuepiClimate {
 public:
  using DuepiClimate::control;
};

class TestLevel : public DuepiLevel {
 public:
  using DuepiLevel::control;
};

void power_preset_tests() {
  const std::vector<std::string> presets = {"Min", "Low", "Medium", "High", "Max"};
  for (uint16_t level = 1; level <= 5; ++level) {
    test_now = 0;
    TestHub hub;
    TestClimate climate;
    TestLevel number;
    climate.set_parent(&hub);
    number.set_parent(&hub);
    hub.set_climate(&climate);
    hub.set_level_number(&number);
    hub.setup();
    CHECK(climate.supported_custom_presets == presets);
    CHECK(!climate.has_custom_preset());
    CHECK(std::isnan(number.state));
    climate::ClimateCall call;
    call.custom_preset = presets[level - 1];
    climate.control(call);
    CHECK(hub.queued() == 0);
    hub.synchronize();
    hub.observe(Command::REQUESTED_POWER, static_cast<uint32_t>(level) << 8);
    climate.control(call);
    CHECK(hub.queued() == 0);
    CHECK(hub.writes.empty());
    CHECK(climate.mode == climate::CLIMATE_MODE_OFF);
    CHECK(climate.get_custom_preset() == call.custom_preset);
    CHECK(number.state == level);
    hub.observe(Command::PHASE, 0x020100);
    hub.observe(Command::TARGET, 0x001500);
    climate.control(call);
    CHECK(hub.queued() == 1);
    CHECK(climate.get_custom_preset() == call.custom_preset);
    CHECK(number.state == level);
    CHECK(climate.target_temperature == 21);
    hub.tick();
    CHECK(hub.writes.size() == 1);
    CHECK(std::string(hub.writes.back().begin() + 1, hub.writes.back().begin() + 7) ==
          "RF00" + std::to_string(level) + "0");
    CHECK(climate.get_custom_preset() == call.custom_preset);
    test_now = 1;
    hub.receive("000000");
    CHECK(climate.get_custom_preset() == call.custom_preset);
    CHECK(number.state == level);
    CHECK(climate.target_temperature == 21);
    hub.observe(Command::D3000, 0x000900);
    CHECK(climate.get_custom_preset() == call.custom_preset);
    for (Outcome outcome : {Outcome::QUEUED, Outcome::SENT, Outcome::REJECTED,
                            Outcome::CANCELLED, Outcome::INDETERMINATE, Outcome::TIMED_OUT}) {
      hub.report({Command::SET_LEVEL, static_cast<uint16_t>(level == 5 ? 1 : level + 1)}, outcome);
      CHECK(climate.get_custom_preset() == call.custom_preset);
      CHECK(number.state == level);
    }
    climate::ClimateCall invalid;
    invalid.custom_preset = "Power level 6";
    climate.control(invalid);
    CHECK(hub.queued() == 0);
    CHECK(climate.get_custom_preset() == call.custom_preset);
    if (level == 1) {
      hub.observe(Command::PHASE, 0);
    } else if (level == 2) {
      climate.control(call);
      hub.observe(Command::PHASE, 0x030100);
      CHECK(hub.queued() == 0);
    } else if (level == 3) {
      test_now = 45001;
      hub.publish();
    } else if (level == 4) {
      hub.recover();
    } else {
      number.control(2);
      CHECK(number.state == 5);
      test_now = 101;
      hub.tick();
      hub.receive("000000");
      CHECK(number.state == 5);
      hub.observe(Command::REQUESTED_POWER, 0x000200);
      CHECK(number.state == 2);
      CHECK(climate.get_custom_preset() == "Low");
      climate.control(call);
      test_now = 201;
      hub.tick();
      test_now = 1001;
      hub.tick();
      CHECK(hub.writes.size() == 3);
    }
    if (level == 1 || level == 2) {
      CHECK(climate.get_custom_preset() == call.custom_preset);
      CHECK(number.state == level);
    } else {
      CHECK(!climate.has_custom_preset());
      CHECK(std::isnan(number.state));
    }
  }
  test_now = 0;
  std::cout << "PASS: climate power presets follow readback only, Off rejection, cancellation, timeout, shutdown, stale and recovery\n";
}

void power_feedback_tests() {
  const char *presets[] = {"Min", "Low", "Medium", "High", "Max"};
  for (uint16_t phase : {0x0000, 0x0401, 0x1000, 0x1001, 0x0301, 0x0801, 0x0802}) {
    test_now = 0;
    TestHub hub;
    TestClimate climate;
    TestLevel number;
    climate.set_parent(&hub);
    number.set_parent(&hub);
    hub.set_climate(&climate);
    hub.set_level_number(&number);
    hub.setup();
    hub.synchronize();
    hub.observe(Command::PHASE, static_cast<uint32_t>(phase) << 8);
    hub.observe(Command::REQUESTED_POWER, 0x000200);
    size_t published = climate.publications;
    climate::ClimateCall call;
    call.custom_preset = "Max";
    climate.control(call);
    number.control(5);
    CHECK(climate.publications > published);
    CHECK(climate.get_custom_preset() == "Low");
    CHECK(number.state == 2);
    CHECK(hub.queued() == 0 && hub.writes.empty());
  }
  {
    test_now = 0;
    TestHub hub;
    TestClimate climate;
    climate.set_parent(&hub);
    hub.set_climate(&climate);
    hub.setup();
    hub.synchronize();
    hub.observe(Command::REQUESTED_POWER, 0x000200);
    climate::ClimateCall call;
    call.custom_preset = "Max";
    call.mode = climate::CLIMATE_MODE_HEAT;
    climate.control(call);
    CHECK(climate.get_custom_preset() == "Low");
    CHECK(hub.queued() == 1);
    hub.tick();
    CHECK(std::string(hub.writes.back().begin() + 1, hub.writes.back().begin() + 7) == "RF0020");
  }
  {
    test_now = 0;
    TestHub hub;
    TestLevel number;
    hub.set_level_number(&number);
    hub.setup();
    hub.synchronize();
    hub.observe(Command::REQUESTED_POWER, 0x000400);
    test_now = 45000;
    hub.synchronize();
    CHECK(std::isnan(number.state));
    CHECK(hub.request_start());
    hub.tick();
    CHECK(std::string(hub.writes.back().begin() + 1, hub.writes.back().begin() + 7) == "RF0010");
  }
  for (uint16_t phase : {0x0101, 0x0201}) {
    test_now = 0;
    TestHub hub;
    TestLevel number;
    TestClimate climate;
    sensor::Sensor reported;
    climate.set_parent(&hub);
    hub.set_climate(&climate);
    hub.set_level_number(&number);
    hub.set_numeric_sensor(static_cast<size_t>(Command::REQUESTED_POWER), &reported);
    hub.setup();
    hub.synchronize();
    CHECK(!hub.request_level(5));
    hub.observe(Command::REQUESTED_POWER, 0x000100);
    CHECK(number.state == 1 && reported.state == 1);
    CHECK(climate.get_custom_preset() == "Min");
    hub.observe(Command::PHASE, static_cast<uint32_t>(phase) << 8);
    hub.observe(Command::REQUESTED_POWER, 0x000200);
    CHECK(number.state == 2 && reported.state == 2);
    CHECK(climate.get_custom_preset() == "Low");
    hub.observe(Command::PHASE, 0);
    CHECK(!hub.request_level(4));
    hub.observe(Command::REQUESTED_POWER, 0x000200);
    CHECK(number.state == 2 && reported.state == 2);
    CHECK(climate.get_custom_preset() == "Low");
    CHECK(hub.writes.empty());
  }
  for (uint16_t level = 1; level <= 5; ++level) {
    test_now = 0;
    TestHub hub;
    TestLevel number;
    TestClimate climate;
    sensor::Sensor reported;
    text_sensor::TextSensor status;
    climate.set_parent(&hub);
    hub.set_climate(&climate);
    hub.set_level_number(&number);
    hub.set_numeric_sensor(static_cast<size_t>(Command::REQUESTED_POWER), &reported);
    hub.set_text_sensor(4, &status);
    hub.setup();
    CHECK(!hub.request_level(level));
    CHECK(hub.writes.empty());
    hub.synchronize();
    hub.observe(Command::REQUESTED_POWER, 0x000500);
    CHECK(number.state == 5 && reported.state == 5);
    CHECK(climate.get_custom_preset() == "Max");
    CHECK(!hub.request_level(level));
    CHECK(hub.writes.empty() && hub.queued() == 0);
    CHECK(number.state == 5);
    CHECK(climate.get_custom_preset() == "Max");
    CHECK(status.state.find("use HEAT to start") != std::string::npos);
    hub.observe(Command::REQUESTED_POWER, 0x000500);
    CHECK(number.state == 5 && reported.state == 5);
    CHECK(hub.request_start());
    hub.tick();
    CHECK(std::string(hub.writes.back().begin() + 1, hub.writes.back().begin() + 7) ==
          "RF0050");
    test_now = 1;
    hub.receive("000000");
    hub.observe(Command::PHASE, 0x010100);
    CHECK(status.state.find("verified by readback") != std::string::npos);
    hub.observe(Command::FIRMWARE, 0xA06000);
    hub.observe(Command::BOARD, 0);
    hub.observe(Command::ROOM, 0x00D700);
    hub.observe(Command::ALARM, 0);
    CHECK(hub.request_level(level));
    test_now = 101;
    hub.tick();
    hub.receive("000000");
    hub.publish();
    CHECK(number.state == 5);
    CHECK(climate.get_custom_preset() == "Max");
    CHECK(status.state.find("value=" + std::to_string(level)) != std::string::npos);
    hub.observe(Command::D4000, static_cast<uint32_t>(level) << 8);
    CHECK(status.state.find("acknowledged, unverified") != std::string::npos);
    hub.observe(Command::REQUESTED_POWER, static_cast<uint32_t>(level) << 8);
    CHECK(reported.state == level && number.state == level);
    CHECK(climate.get_custom_preset() == presets[level - 1]);
    CHECK(status.state.find("verified by readback") != std::string::npos);
    hub.observe(Command::PHASE, 0x020100);
    CHECK(hub.request_level(3));
    test_now = 201;
    hub.tick();
    hub.receive("000000");
    hub.observe(Command::REQUESTED_POWER, 0x000100);
    CHECK(status.state.find("requested=3 reported=1: readback mismatch") != std::string::npos);
    CHECK(number.state == 1);
    CHECK(climate.get_custom_preset() == "Min");
    CHECK(hub.request_level(4));
    test_now = 301;
    hub.tick();
    hub.receive("000000");
    hub.observe(Command::REQUESTED_POWER, 0);
    CHECK(std::isnan(reported.state));
    CHECK(std::isnan(number.state));
    CHECK(!climate.has_custom_preset());
    CHECK(status.state.find("readback unavailable") != std::string::npos);
    CHECK(hub.request_level(2));
    test_now = 401;
    hub.tick();
    hub.receive("000000");
    test_now = 15401;
    hub.observe(Command::REQUESTED_POWER, 0x000200);
    CHECK(status.state.find("readback unavailable") != std::string::npos);
    CHECK(status.state.find("verified by readback") == std::string::npos);
    test_now = 60401;
    hub.observe(Command::PHASE, 0x020100);
    hub.observe(Command::ROOM, 0x00D700);
    hub.observe(Command::ALARM, 0);
    CHECK(std::isnan(reported.state));
    CHECK(std::isnan(number.state));
    CHECK(!climate.has_custom_preset());
  }
  {
    test_now = 0;
    TestHub hub;
    TestLevel number;
    sensor::Sensor reported;
    text_sensor::TextSensor status;
    hub.set_level_number(&number);
    hub.set_numeric_sensor(static_cast<size_t>(Command::REQUESTED_POWER), &reported);
    hub.set_text_sensor(4, &status);
    hub.setup();
    hub.synchronize();
    hub.observe(Command::PHASE, 0x020100);
    CHECK(hub.request_level(3));
    hub.tick();
    test_now = 1;
    hub.receive("000000");
    const char *replies[] = {"A06000", "000000", "020100", "00D700", "000000", "000300"};
    for (size_t index = 0; index < 6; ++index) {
      test_now = 101 + index * 100;
      hub.poll();
      CHECK(hub.writes.size() == index + 2);
      if (index == 5)
        CHECK(std::string(hub.writes.back().begin() + 1, hub.writes.back().begin() + 7) == "RC5000");
      hub.receive(replies[index]);
    }
    hub.publish();
    CHECK(number.state == 3 && reported.state == 3);
    CHECK(status.state.find("verified by readback") != std::string::npos);
    hub.recover();
    hub.publish();
    CHECK(std::isnan(number.state));
  }
  for (bool enabled : {false, true}) {
    test_now = 0;
    TestHub hub;
    hub.set_read_power_level(enabled);
    hub.setup();
    const char *replies[] = {"A06000", "000000", "000000", "00D700", "000000", "000300"};
    for (size_t index = 0; index < 6; ++index) {
      hub.poll();
      CHECK(hub.writes.size() == index + 1);
      if (index == 5)
        CHECK(std::string(hub.writes.back().begin() + 1, hub.writes.back().begin() + 7) ==
              (enabled ? "RC5000" : "RC6000"));
      hub.receive(replies[index]);
      test_now += 100;
    }
  }
  for (FirmwareProfile profile : {FirmwareProfile::FW_9200, FirmwareProfile::FW_E670, FirmwareProfile::FW_E693}) {
    test_now = 0;
    TestHub hub;
    TestLevel number;
    hub.set_level_number(&number);
    hub.set_firmware_profile(profile);
    hub.setup();
    hub.synchronize();
    CHECK(!hub.request_level(3));
    CHECK(std::isnan(number.state));
    CHECK(hub.request_start());
    hub.tick();
    CHECK(std::string(hub.writes.back().begin() + 1, hub.writes.back().begin() + 7) ==
          (profile == FirmwareProfile::FW_9200 ? "RF0050" : "RF0060"));
        test_now = 1;
        hub.receive("000000");
        hub.observe(Command::PHASE, 0x010100);
        hub.observe(Command::FIRMWARE, 0xA06000);
        hub.observe(Command::BOARD, 0);
        hub.observe(Command::ROOM, 0x00D700);
        hub.observe(Command::ALARM, 0);
        hub.observe(Command::REQUESTED_POWER, 0x000200);
        CHECK(number.state == 2);
  }
  test_now = 0;
  std::cout << "PASS: C500-only selection, Off rejection, reported-level startup, ignition writes, match/mismatch/unavailable/expiry, optional polling and legacy overrides\n";
}

void normal_tests() {
  TestHub hub;
  DuepiClimate climate;
  text_sensor::TextSensor status;
  text_sensor::TextSensor last_response;
  sensor::Sensor target;
  climate.set_parent(&hub);
  hub.set_climate(&climate);
  hub.set_text_sensor(4, &status);
  hub.set_text_sensor(7, &last_response);
  hub.set_numeric_sensor(8, &target);
  hub.setup();
  CHECK(hub.writes.empty());
  CHECK(std::isnan(climate.target_temperature));
  CHECK(std::isnan(climate.current_temperature));
  CHECK(std::isnan(target.state));
  CHECK(last_response.state == "Unknown");
  CHECK(!hub.request_start());
  hub.synchronize();
  CHECK(last_response.state == "0d 00h 00m 00s");
  test_now = 90061000;
  hub.observe(Command::PCB, 0x001500);
  CHECK(last_response.state == "1d 01h 01m 01s");
  test_now = 0;
  hub.observe(Command::D4000, 0x000500);
  hub.observe(Command::TARGET, 0x001500);
  hub.tick();
  CHECK(hub.writes.empty());
  CHECK(climate.target_temperature == 21);
  CHECK(climate.current_temperature == 21.5f);
  size_t published = climate.publications;
  hub.publish();
  CHECK(climate.publications == published);
  CHECK(hub.writes.empty());
  CHECK(hub.request_target(24));
  CHECK(hub.request_target(25));
  hub.tick();
  CHECK(hub.writes.size() == 1);
  CHECK(climate.target_temperature == 21);
  test_now = 1;
  hub.receive("000000");
  CHECK(status.state.find("acknowledged, unverified") != std::string::npos);
  hub.observe(Command::ROOM, 0x00D700);
  CHECK(status.state.find("acknowledged, unverified") != std::string::npos);
  hub.observe(Command::TARGET, 0x001900);
  CHECK(status.state.find("verified by readback") != std::string::npos);
  CHECK(climate.target_temperature == 25);
  test_now = 45001;
  hub.publish();
  CHECK(std::isnan(climate.current_temperature));
  CHECK(climate.target_temperature == 25);
  CHECK(std::isnan(target.state));
  CHECK(!hub.request_target(24));
  CHECK(hub.writes.size() == 1);
  hub.synchronize();
  CHECK(hub.request_start());
  CHECK(!hub.request_level(3));
  CHECK(hub.request_off());
  hub.tick();
  CHECK(hub.writes.size() == 2);
  CHECK(hub.writes.back()[2] == 'F' && hub.writes.back()[5] == '0');
  hub.recover();
  test_now += 1000;
  hub.tick();
  CHECK(!hub.request_start());
  CHECK(hub.writes.size() == 2);
  hub.synchronize();
  CHECK(hub.writes.size() == 2);
  hub.observe(Command::D3000, 0x000300);
  hub.observe(Command::D4000, 0x000400);
  CHECK(hub.writes.size() == 2);
  std::cout << "PASS: actual hub/climate boot, observations, write gating, coalescing, ACK/readback, stale, resync\n";
  for (uint32_t firmware : {0x920000U, 0xE67000U, 0xE69300U, 0x123400U}) {
    test_now = 0;
    TestHub starting;
    text_sensor::TextSensor outcome;
    starting.set_text_sensor(4, &outcome);
    starting.setup();
    CHECK(!starting.request_start());
    CHECK(starting.writes.empty());
    starting.synchronize();
    starting.observe(Command::FIRMWARE, firmware);
    CHECK(starting.request_start());
    starting.tick();
    CHECK(starting.writes.size() == 1);
    const char *expected = "RF0010";
    CHECK(std::string(starting.writes[0].begin() + 1, starting.writes[0].begin() + 7) == expected);
    test_now = 1;
    starting.receive("000000");
    CHECK(outcome.state.find("acknowledged, unverified") != std::string::npos);
    CHECK(!starting.request_target(21));
    starting.observe(Command::PHASE, 0x010100);
    CHECK(outcome.state.find("verified by readback") != std::string::npos);
    starting.synchronize();
    starting.observe(Command::FIRMWARE, firmware);
    CHECK(starting.request_target(21));
    starting.observe(Command::PHASE, 0x020100);
    CHECK(starting.request_level(3));
  }
  std::cout << "PASS: known and generic ignition frames, ACK resync, readback, target and level access\n";
}

void target_feedback_tests() {
  test_now = 0;
  TestHub hub;
  DuepiClimate climate;
  sensor::Sensor observed;
  text_sensor::TextSensor source;
  text_sensor::TextSensor outcome;
  climate.set_parent(&hub);
  hub.set_climate(&climate);
  hub.set_numeric_sensor(8, &observed);
  hub.set_text_sensor(8, &source);
  hub.set_text_sensor(4, &outcome);
  hub.setup();
  CHECK(std::isnan(climate.target_temperature));
  CHECK(source.state == "Unknown");
  hub.synchronize();
  hub.read_target();
  test_now = 1;
  hub.receive("000000");
  hub.publish();
  CHECK(std::isnan(observed.state) && std::isnan(climate.target_temperature));
  CHECK(source.state == "Unknown");
  CHECK(hub.request_target(22));
  test_now = 101;
  hub.tick();
  hub.publish();
  CHECK(hub.writes.size() == 2);
  CHECK(std::isnan(climate.target_temperature));
  hub.receive("000000");
  hub.publish();
  CHECK(climate.target_temperature == 22);
  CHECK(std::isnan(observed.state));
  CHECK(source.state == "Acknowledged request (unverified)");
  hub.observe(Command::TARGET, 0);
  CHECK(climate.target_temperature == 22 && std::isnan(observed.state));
  CHECK(outcome.state.find("verified by readback") == std::string::npos);
  hub.observe(Command::TARGET, 0x001500);
  CHECK(climate.target_temperature == 21 && observed.state == 21);
  CHECK(source.state == "Reported");
  CHECK(hub.request_target(24));
  test_now = 201;
  hub.tick();
  hub.receive("000000");
  hub.publish();
  CHECK(climate.target_temperature == 21 && source.state == "Reported");
  hub.observe(Command::TARGET, 0xFFFF00);
  CHECK(climate.target_temperature == 24 && std::isnan(observed.state));
  CHECK(source.state == "Acknowledged request (unverified)");
  CHECK(outcome.state.find("acknowledged, unverified") != std::string::npos);
  for (Outcome failure : {Outcome::REJECTED, Outcome::CANCELLED, Outcome::INDETERMINATE, Outcome::TIMED_OUT}) {
    hub.report({Command::SET_TARGET, 25, 99}, failure);
    hub.publish();
    CHECK(climate.target_temperature == 24 && std::isnan(observed.state));
    CHECK(source.state == "Acknowledged request (unverified)");
  }
  hub.observe(Command::TARGET, 0x001800);
  CHECK(climate.target_temperature == 24 && observed.state == 24);
  CHECK(source.state == "Reported");
  CHECK(outcome.state.find("verified by readback") != std::string::npos);
  test_now = 45201;
  hub.publish();
  CHECK(climate.target_temperature == 24 && std::isnan(observed.state));
  CHECK(source.state == "Retained reading (unverified)");
  CHECK(hub.writes.size() == 3);

  TestHub rebooted;
  DuepiClimate rebooted_climate;
  rebooted_climate.set_parent(&rebooted);
  rebooted.set_climate(&rebooted_climate);
  rebooted.set_text_sensor(8, &source);
  rebooted.setup();
  CHECK(std::isnan(rebooted_climate.target_temperature) && source.state == "Unknown");
  CHECK(rebooted.writes.empty());

  for (bool writing : {false, true}) {
    test_now = 0;
    TestHub timed_out;
    DuepiClimate timed_out_climate;
    timed_out_climate.set_parent(&timed_out);
    timed_out.set_climate(&timed_out_climate);
    timed_out.set_text_sensor(8, &source);
    timed_out.setup();
    timed_out.synchronize();
    if (writing) {
      CHECK(timed_out.request_target(23));
      timed_out.tick();
    } else {
      timed_out.read_target();
    }
    test_now = 800;
    timed_out.tick();
    timed_out.publish();
    CHECK(!timed_out.request_target(23));
    CHECK(std::isnan(timed_out_climate.target_temperature) && source.state == "Unknown");
    CHECK(timed_out.writes.size() == 1);
  }
  std::cout << "PASS: unavailable target replies, ACK-only display fallback, source, readback, stale, reboot and genuine timeouts\n";
}

#ifdef USE_DUEPI_INSTALLER
void prepare_installer(TestHub &hub) {
  hub.setup();
  hub.set_enable_installer(true);
  CHECK(hub.set_installer_enabled(true));
  hub.synchronize();
}

void standby_tests() {
  test_now = 0;
  TestHub hub;
  TestStandbySwitch standby;
  standby.set_parent(&hub);
  hub.set_standby_mode(&standby, 28);
  hub.setup();
  CHECK(!standby.known() && hub.writes.empty());
  CHECK(!hub.request_standby(true));
  hub.synchronize();
  CHECK(!hub.installer_read(28));
  CHECK(!hub.request_installer_write(28, 1));
  Request read{Command::INSTALLER_READ};
  read.parameter = 28;
  read.standby = true;
  CHECK(hub.queue(read));
  hub.tick();
  CHECK(std::string(hub.writes.back().begin() + 1, hub.writes.back().begin() + 7) == "L1C000");
  hub.receive("000000");
  CHECK(standby.known() && !standby.state);
  standby.change(true);
  CHECK(!standby.state);
  test_now = 100;
  hub.tick();
  CHECK(std::string(hub.writes.back().begin() + 1, hub.writes.back().begin() + 7) == "S1C010");
  hub.receive("000000");
  CHECK(!standby.state);
  CHECK(!hub.request_standby(false));
  CHECK(!hub.set_installer_access(false));
  test_now = 200;
  hub.tick();
  CHECK(std::string(hub.writes.back().begin() + 1, hub.writes.back().begin() + 7) == "L1C000");
  hub.receive("010000");
  CHECK(standby.state);
  test_now = 1200;
  hub.tick();
  hub.synchronize();
  CHECK(hub.request_standby(false));
  hub.tick();
  CHECK(std::string(hub.writes.back().begin() + 1, hub.writes.back().begin() + 7) == "S1C000");
  hub.receive("000000");
  CHECK(standby.state);
  test_now += 100;
  hub.tick();
  hub.receive("010000");
  CHECK(standby.state);
  test_now += 1000;
  hub.tick();
  hub.synchronize();
  CHECK(hub.queue(read));
  hub.tick();
  hub.receive("020000");
  CHECK(standby.state);
  Request wrong{Command::INSTALLER_READ};
  wrong.parameter = 29;
  wrong.standby = true;
  CHECK(hub.queue(wrong));
  size_t sent = hub.writes.size();
  test_now += 100;
  hub.tick();
  CHECK(hub.writes.size() == sent);

  for (uint16_t phase : {0x0000, 0x0200, 0x0201, 0x0301, 0x0401, 0x0801, 0x0802, 0x1000,
                         0x0101, 0x010A, 0x0133, 0x9999}) {
    for (bool send_time : {false, true}) {
      test_now = 0;
      TestHub gated;
      TestStandbySwitch entity;
      entity.set_parent(&gated);
      gated.set_standby_mode(&entity, 7);
      gated.setup();
      gated.synchronize();
      bool permitted = known_phase(phase) && (phase >> 8) != 1;
      if (send_time) {
        CHECK(gated.request_standby(true));
        gated.observe(Command::PHASE, static_cast<uint32_t>(phase) << 8);
      } else {
        gated.observe(Command::PHASE, static_cast<uint32_t>(phase) << 8);
        CHECK(gated.request_standby(true) == permitted);
      }
      gated.tick();
      CHECK(gated.writes.size() == (permitted ? 1U : 0U));
      if (permitted)
        CHECK(std::string(gated.writes.back().begin() + 1, gated.writes.back().begin() + 7) == "S07010");
    }
  }
  test_now = 0;
  TestHub polling;
  TestStandbySwitch polled;
  polled.set_parent(&polling);
  polling.set_standby_mode(&polled, 28);
  polling.setup();
  bool found = false;
  for (size_t index = 0; index < 20 && !found; ++index) {
    polling.poll();
    std::string payload(polling.writes.back().begin() + 1, polling.writes.back().begin() + 7);
    CHECK(payload[0] != 'S');
    if (payload == "L1C000") {
      polling.receive("010000");
      found = true;
    } else if (payload == "RDC000") polling.receive("A06000");
    else if (payload == "RD9000") polling.receive("020100");
    else if (payload == "RD1000") polling.receive("00D700");
    else if (payload == "RC5000") polling.receive("000200");
    else if (payload == "RC6000") polling.receive("001400");
    else polling.receive("000000");
    test_now += 100;
  }
  CHECK(found && polled.known() && polled.state);
  test_now += 45001;
  CHECK(!polling.request_standby(false));

  test_now = 0;
  TestHub timeout;
  TestStandbySwitch unchanged;
  unchanged.set_parent(&timeout);
  timeout.set_standby_mode(&unchanged, 28);
  timeout.setup();
  timeout.synchronize();
  unchanged.publish_observed(false);
  CHECK(timeout.request_standby(true));
  timeout.tick();
  timeout.receive("000000");
  for (uint32_t now : {100U, 800U, 900U, 1600U, 3000U}) {
    test_now = now;
    timeout.tick();
  }
  CHECK(!unchanged.state);
  timeout.synchronize();
  timeout.tick();
  size_t parameter_writes = 0;
  for (const auto &frame : timeout.writes)
    if (frame[1] == 'S') ++parameter_writes;
  CHECK(parameter_writes == 1);

  test_now = 0;
  TestHub raw;
  raw.set_enable_installer(true);
  raw.set_enable_custom_commands(true);
  raw.set_enable_raw_commands(true);
  raw.setup();
  raw.synchronize();
  CHECK(raw.set_installer_access(true));
  CHECK(raw.request_custom("S1C010", true));
  raw.observe(Command::PHASE, 0x010100);
  raw.tick();
  CHECK(raw.writes.empty());
  CHECK(!raw.request_custom("S1C010", true));
  std::cout << "PASS: scoped standby permission, read-only boot/polling, exact address, ACK/readback separation, mismatch, invalid value and ignition guards\n";
}

void installer_tests() {
  test_now = 0;
  TestHub direct_write;
  CHECK(!direct_write.request_installer_write(17, 42));
  prepare_installer(direct_write);
  CHECK(direct_write.request_installer_write(17, 42));
  direct_write.tick();
  CHECK(direct_write.writes.size() == 1 && direct_write.writes[0][1] == 'S');
  for (bool raw : {false, true}) {
    TestHub direct_custom;
    CHECK(!direct_custom.request_custom(raw ? "RD1000" : "D100", raw));
    prepare_installer(direct_custom);
    direct_custom.set_enable_custom_commands(true);
    direct_custom.set_enable_raw_commands(true);
    CHECK(direct_custom.set_installer_access(true));
    CHECK(!direct_custom.request_custom("invalid", raw));
    CHECK(direct_custom.request_custom(raw ? "RD1000" : "D100", raw));
    direct_custom.set_installer_access(false);
    direct_custom.tick();
    CHECK(direct_custom.writes.empty());
    CHECK(direct_custom.set_installer_access(true));
    CHECK(direct_custom.request_custom(raw ? "RD1000" : "D100", raw));
    direct_custom.tick();
    CHECK(direct_custom.writes.size() == 1);
  }
  TestHub access;
  access.setup();
  access.set_enable_installer(true);
  access.set_enable_custom_commands(true);
  access.set_enable_raw_commands(true);
  access.synchronize();
  CHECK(!access.installer_read(1));
  CHECK(!access.arm_installer_write());
  CHECK(!access.arm_custom_command());
  CHECK(access.set_installer_access(true));
  CHECK(access.arm_installer_write());
  CHECK(access.arm_custom_command());
  CHECK(!access.set_installer_access(false));
  CHECK(!access.installer_read(1));
  CHECK(!access.installer_write(1, 42));
  CHECK(!access.send_custom("D100", false));
  CHECK(!access.send_custom("RD1000", true));
  CHECK(access.set_installer_access(true));
  CHECK(!access.installer_write(1, 42));
  CHECK(!access.send_custom("D100", false));
  CHECK(access.request_target(21));
  CHECK(!access.set_installer_access(false));
  access.tick();
  CHECK(access.writes.size() == 1);
  TestInstallerSwitch access_switch;
  access_switch.set_parent(&access);
  access.set_privilege_switch(0, &access_switch);
  access_switch.change(true);
  CHECK(access_switch.state);
  CHECK(access.arm_custom_command());
  access.setup();
  CHECK(!access_switch.state);
  CHECK(!access.arm_custom_command());
  CHECK(!access.arm_installer_write());
  TestInstallerValue parameter;
  TestInstallerValue proposed;
  access.set_installer_number(false, &parameter);
  access.set_installer_number(true, &proposed);
  access.setup();
  CHECK(parameter.state == 1 && proposed.state == 0);
  parameter.change(17);
  proposed.change(42);
  for (float value : {-1.0f, 256.0f, 1.5f, NAN, INFINITY}) {
    parameter.change(value);
    proposed.change(value);
    CHECK(parameter.state == 17 && proposed.state == 42);
  }
  CHECK(!access.installer_selected(false));
  CHECK(!access.installer_selected(true));
  CHECK(access.writes.size() == 1);
  TestHub selected;
  selected.set_installer_number(false, &parameter);
  selected.set_installer_number(true, &proposed);
  prepare_installer(selected);
  CHECK(parameter.state == 1 && proposed.state == 0);
  parameter.change(17);
  proposed.change(42);
  CHECK(selected.installer_selected(true));
  selected.tick();
  CHECK(selected.writes.size() == 1);
  CHECK(std::string(selected.writes[0].begin() + 1, selected.writes[0].begin() + 7) == "S112A0");
  TestHub hub;
  text_sensor::TextSensor installer;
  hub.set_text_sensor(5, &installer);
  CHECK(!hub.set_installer_enabled(true));
  CHECK(!hub.installer_read(1));
  prepare_installer(hub);
  CHECK(!hub.installer_write(1, 42));
  CHECK(installer.state.find("not armed") != std::string::npos);
  CHECK(hub.installer_read(1));
  hub.tick();
  CHECK(hub.writes.size() == 1 && hub.writes.back()[1] == 'L');
  hub.receive("2A0000");
  CHECK(installer.state.find("read value 42") != std::string::npos);
  CHECK(hub.arm_installer_write());
  CHECK(!hub.installer_write(-1, 43));
  CHECK(installer.state.find("write rejected: range") != std::string::npos);
  CHECK(!hub.installer_write(1, 256));
  CHECK(hub.installer_write(1, 43));
  CHECK(!hub.installer_write(1, 44));
  test_now = 100;
  hub.tick();
  CHECK(hub.writes.size() == 2 && hub.writes.back()[1] == 'S');
  CHECK(hub.request_start());
  hub.receive("000000");
  CHECK(installer.state.find("awaiting readback") != std::string::npos);
  CHECK(!hub.request_start());
  test_now = 200;
  hub.tick();
  CHECK(hub.writes.size() == 3 && hub.writes.back()[1] == 'L');
  hub.receive("2B0000");
  CHECK(installer.state.find("verified") != std::string::npos);
  CHECK(!hub.request_start());
  test_now = 1000;
  hub.tick();
  CHECK(hub.writes.size() == 3);

  TestHub mismatch;
  text_sensor::TextSensor mismatch_status;
  mismatch.set_text_sensor(5, &mismatch_status);
  prepare_installer(mismatch);
  CHECK(mismatch.arm_installer_write());
  CHECK(mismatch.installer_write(1, 43));
  mismatch.tick();
  mismatch.receive("000000");
  test_now += 100;
  mismatch.tick();
  mismatch.receive("2A0000");
  CHECK(mismatch_status.state.find("MISMATCH") != std::string::npos);

  TestHub expired;
  prepare_installer(expired);
  expired.set_arming_timeout(1000);
  expired.set_response_timeout(5000);
  CHECK(expired.installer_read(2));
  expired.tick();
  CHECK(expired.arm_installer_write());
  CHECK(expired.installer_write(2, 1));
  test_now += 1000;
  expired.receive("010000");
  test_now += 100;
  expired.tick();
  CHECK(expired.writes.size() == 1);
  CHECK(expired.arm_installer_write());
  CHECK(expired.installer_write(2, 1));
  expired.set_installer_enabled(false);
  test_now += 100;
  expired.tick();
  CHECK(expired.writes.size() == 1);
  TestHub timed_out;
  text_sensor::TextSensor timeout_status;
  timed_out.set_text_sensor(5, &timeout_status);
  prepare_installer(timed_out);
  CHECK(timed_out.arm_installer_write());
  CHECK(timed_out.installer_write(1, 2));
  timed_out.tick();
  timed_out.receive("000001");
  test_now += 800;
  timed_out.tick();
  CHECK(timeout_status.state.find("indeterminate") != std::string::npos);
  test_now += 1000;
  timed_out.tick();
  CHECK(timed_out.writes.size() == 1);
  TestHub identity;
  text_sensor::TextSensor identity_status;
  identity.set_text_sensor(5, &identity_status);
  prepare_installer(identity);
  CHECK(identity.installer_read(1));
  CHECK(identity.installer_read(2));
  identity.tick();
  identity.receive("010000");
  test_now += 100;
  identity.tick();
  CHECK(identity.writes.size() == 2 && identity.writes.back()[3] == '2');
  identity.receive("020000");
  CHECK(identity_status.state.find("PR2 read value 2") != std::string::npos);
  identity.observe(Command::PHASE, 0x020100);
  CHECK(identity.arm_installer_write());
  CHECK(identity.installer_write(1, 2));
  identity.set_installer_enabled(false);
  CHECK(!identity.installer_read(1));
  CHECK(identity_status.state.find("read rejected") != std::string::npos);
  std::cout << "PASS: installer runtime enable, byte ranges, one-shot, queued expiry, readback match/mismatch, cancellation\n";
}

void installer_readback_failure_tests() {
  for (bool disable_installer : {true, false}) {
    test_now = 0;
    TestHub hub;
    text_sensor::TextSensor status;
    hub.set_text_sensor(5, &status);
    prepare_installer(hub);
    CHECK(hub.arm_installer_write());
    CHECK(hub.installer_write(1, 42));
    hub.tick();
    hub.receive("000000");
    CHECK(status.state.find("awaiting readback") != std::string::npos);
    if (disable_installer) hub.set_installer_enabled(false);
    else hub.set_custom_enabled(false);
    CHECK(status.state.find("write acknowledged; readback cancelled; write unverified") != std::string::npos);
    CHECK(!hub.request_off());
    CHECK(hub.set_installer_enabled(true));
    hub.synchronize();
    CHECK(hub.installer_read(2));
    CHECK(hub.request_target(20));
    CHECK(hub.request_off());
    CHECK(hub.writes.size() == 1);
  }
  for (Outcome outcome : {Outcome::REJECTED, Outcome::TIMED_OUT}) {
    test_now = 0;
    TestHub hub;
    text_sensor::TextSensor status;
    hub.set_text_sensor(5, &status);
    prepare_installer(hub);
    CHECK(hub.arm_installer_write());
    CHECK(hub.installer_write(1, 42));
    hub.tick();
    hub.receive("000000");
    hub.report({Command::INSTALLER_READ, 0, 1, 1}, outcome);
    CHECK(status.state.find("write acknowledged; readback") != std::string::npos);
    CHECK(status.state.find("write unverified") != std::string::npos);
    hub.synchronize();
    CHECK(hub.request_off());
  }
  for (bool expire_queue : {true, false}) {
    test_now = 0;
    TestHub hub;
    text_sensor::TextSensor status;
    hub.set_text_sensor(5, &status);
    prepare_installer(hub);
    CHECK(hub.arm_installer_write());
    CHECK(hub.installer_write(1, 42));
    hub.tick();
    hub.receive("000000");
    if (expire_queue) {
      test_now = 5000;
      hub.tick();
      CHECK(hub.writes.size() == 1);
    } else {
      test_now = 100;
      hub.tick();
      CHECK(hub.writes.size() == 2 && hub.writes.back()[1] == 'L');
      test_now = 900;
      hub.tick();
      CHECK(status.state.find("write unverified") != std::string::npos);
      test_now = 1150;
      hub.tick();
      CHECK(hub.writes.size() == 2);
      CHECK(status.state.find("readback rejected; write unverified") != std::string::npos);
    }
    CHECK(status.state.find("write unverified") != std::string::npos);
    CHECK(!hub.request_off());
    if (!expire_queue) {
      test_now += 250;
      hub.tick();
    }
    hub.synchronize();
    CHECK(hub.request_off());
    CHECK(hub.installer_read(2));
    size_t writes = 0;
    for (const auto &frame : hub.writes) if (frame[1] == 'S') ++writes;
    CHECK(writes == 1);
  }
  std::cout << "PASS: cancelled/rejected/timed-out installer readback releases busy state, preserves unverified status and resynchronizes\n";
}

void installer_disabled_before_ack_test() {
  test_now = 0;
  TestHub hub;
  DuepiClimate climate;
  binary_sensor::BinarySensor communication;
  text_sensor::TextSensor status;
  climate.set_parent(&hub);
  hub.set_climate(&climate);
  hub.set_communication_sensor(&communication);
  hub.set_text_sensor(5, &status);
  prepare_installer(hub);
  CHECK(communication.state);
  CHECK(hub.request_installer_write(1, 42));
  hub.tick();
  CHECK(!hub.set_installer_access(false));
  hub.receive("000000");
  CHECK(status.state.find("readback disabled, unverified") != std::string::npos);
  CHECK(!communication.state && std::isnan(climate.current_temperature));
  CHECK(!hub.request_start());
  CHECK(!hub.request_off());
  CHECK(!hub.request_target(20));
  CHECK(hub.writes.size() == 1);
  for (const char *payload : {"920000", "000000", "000000", "00D700", "000000"}) {
    test_now += 100;
    hub.loop();
    hub.receive(payload);
  }
  CHECK(hub.request_start());
  test_now += 100;
  hub.tick();
  CHECK(std::string(hub.writes.back().begin() + 1, hub.writes.back().begin() + 7) == "RF0010");
  for (const auto &frame : hub.writes) CHECK(frame[1] != 'L');
  std::cout << "PASS: disabling access before installer ACK invalidates observations and requires a fresh control cycle\n";
}

void custom_response_failure_tests() {
  for (int failure = 0; failure < 4; ++failure) {
    test_now = 0;
    TestHub hub;
    text_sensor::TextSensor response;
    hub.set_text_sensor(6, &response);
    prepare_installer(hub);
    hub.set_enable_custom_commands(true);
    hub.set_enable_raw_commands(true);
    CHECK(hub.set_installer_access(true));
    CHECK(hub.request_custom("D100", false));
    hub.tick();
    hub.receive("00D700");
    CHECK(response.state == "00D700");
    test_now = 1000;
    hub.tick();
    hub.synchronize();
    if (failure == 2) CHECK(!hub.set_installer_access(false));
    if (failure == 3) test_now += 45000;
    CHECK(!hub.request_custom(failure == 0 ? "D10G" : failure == 1 ? "BAD&00" : "D100", failure == 1));
    CHECK(response.state == "Unavailable");
  }
  {
    test_now = 0;
    TestHub hub;
    text_sensor::TextSensor response;
    text_sensor::TextSensor transaction;
    hub.set_text_sensor(6, &response);
    hub.set_text_sensor(4, &transaction);
    prepare_installer(hub);
    hub.set_enable_custom_commands(true);
    CHECK(hub.set_installer_access(true));
    CHECK(hub.request_custom("D100", false));
    hub.tick();
    CHECK(!hub.request_custom("D10G", false));
    std::string rejected = transaction.state;
    hub.receive("00D700");
    CHECK(response.state == "Unavailable");
    CHECK(transaction.state == rejected);
    CHECK(!hub.request_start());
  }
  {
    test_now = 0;
    TestHub hub;
    text_sensor::TextSensor response;
    hub.set_text_sensor(6, &response);
    prepare_installer(hub);
    hub.set_enable_custom_commands(true);
    CHECK(hub.set_installer_access(true));
    CHECK(hub.request_custom("D100", false));
    CHECK(hub.request_custom("D000", false));
    hub.tick();
    CHECK(hub.writes.empty() && response.state == "Pending");
    hub.tick();
    CHECK(hub.writes.size() == 1 && response.state == "Pending");
    hub.receive("012C00");
    CHECK(response.state == "012C00");
  }
  std::cout << "PASS: rejected custom attempts clear old data and older outcomes cannot overwrite the latest result\n";
}

void custom_tests() {
  test_now = 0;
  TestHub hub;
  DuepiClimate climate;
  text_sensor::TextSensor custom;
  climate.set_parent(&hub);
  hub.set_climate(&climate);
  hub.set_text_sensor(6, &custom);
  hub.setup();
  hub.synchronize();
  CHECK(!hub.arm_custom_command());
  hub.set_enable_custom_commands(true);
  CHECK(hub.set_custom_enabled(true));
  CHECK(!hub.send_custom("D100", false));
  CHECK(hub.arm_custom_command());
  CHECK(!hub.send_custom("L01000", true));
  CHECK(!hub.send_custom("D10&", false));
  CHECK(hub.send_custom("D100", false));
  CHECK(custom.state == "Pending");
  CHECK(!hub.send_custom("D100", false));
  CHECK(hub.request_start());
  hub.tick();
  CHECK(hub.writes.size() == 1);
  CHECK(std::isnan(climate.current_temperature));
  hub.receive("00D700");
  CHECK(custom.state == "00D700");
  CHECK(std::isnan(climate.current_temperature));
  test_now = 1000;
  hub.tick();
  CHECK(hub.writes.size() == 1);
  CHECK(!hub.request_start());
  hub.synchronize();
  hub.set_enable_raw_commands(true);
  CHECK(hub.arm_custom_command());
  CHECK(hub.send_custom("L01000", true));
  CHECK(custom.state == "Pending");
  hub.tick();
  CHECK(hub.writes.size() == 2 && hub.writes.back()[1] == 'L');
  test_now += 800;
  hub.tick();
  test_now += 1000;
  hub.tick();
  CHECK(hub.writes.size() == 2);
  CHECK(custom.state == "Unavailable");
  hub.synchronize();
  CHECK(hub.arm_custom_command());
  CHECK(hub.send_custom("D100", false));
  CHECK(custom.state == "Pending");
  hub.set_custom_enabled(false);
  CHECK(custom.state == "Unavailable");
  CHECK(hub.writes.size() == 2);
  std::cout << "PASS: custom opt-in, one-shot, bounded input, raw opt-in, diagnostic-only response, no retry/replay\n";
}

#endif

void verification_expiry_test() {
  test_now = 0;
  TestHub hub;
  text_sensor::TextSensor status;
  hub.set_text_sensor(4, &status);
  hub.setup();
  hub.synchronize();
  CHECK(hub.request_target(20));
  hub.tick();
  hub.receive("000000");
  test_now = 15000;
  hub.observe(Command::TARGET, 0x001400);
  CHECK(status.state.find("acknowledged, unverified") != std::string::npos);
  std::cout << "PASS: expired target verification cannot claim later readback success\n";
}

void temperature_limit_tests() {
  test_now = 0;
  TestHub hub;
  DuepiClimate climate;
  climate.set_parent(&hub);
  hub.set_climate(&climate);
  hub.setup();
  hub.synchronize();
  CHECK(hub.min_temperature() == 10 && hub.max_temperature() == 25);
  CHECK(!hub.request_target(26));
  CHECK(hub.request_target(25));
  hub.set_max_temperature(24);
  hub.tick();
  CHECK(hub.writes.empty());
  hub.set_min_temperature(12);
  hub.set_max_temperature(30);
  CHECK(!hub.request_target(11));
  CHECK(!hub.request_target(31));
  CHECK(!hub.request_target(22.5f));
  CHECK(hub.request_target(30));
  hub.tick();
  CHECK(hub.writes.size() == 1);
  hub.observe(Command::TARGET, 0x002000);
  CHECK(climate.target_temperature == 32);
  std::cout << "PASS: default/configured setpoint bounds, send-time recheck, observed target independence\n";
}

void initialization_hub_tests() {
  test_now = 0;
  TestHub hub;
  text_sensor::TextSensor status;
  hub.set_text_sensor(4, &status);
  hub.setup();
  CHECK(!hub.init_command());
  hub.set_init_command(true);
  hub.synchronize();
  CHECK(hub.request_target(20));
  hub.tick();
  CHECK(hub.writes.size() == 1 && hub.writes.back()[2] == 'D' && hub.writes.back()[3] == 'C');
  hub.receive("920000");
  CHECK(status.state.find("queued") != std::string::npos);
  test_now = 100;
  hub.tick();
  CHECK(hub.writes.size() == 2 && hub.writes.back()[2] == 'F');
  hub.receive("000000");
  CHECK(status.state.find("acknowledged, unverified") != std::string::npos);
  hub.set_init_command(false);
  CHECK(hub.request_target(21));
  test_now = 200;
  hub.tick();
  CHECK(hub.writes.size() == 3 && hub.writes.back()[2] == 'F');

  #ifdef USE_DUEPI_INSTALLER
  TestHub installer;
  prepare_installer(installer);
  installer.set_init_command(true);
  installer.set_arming_timeout(1000);
  CHECK(installer.arm_installer_write());
  CHECK(installer.installer_write(1, 2));
  installer.tick();
  CHECK(installer.writes.size() == 1 && installer.writes.back()[3] == 'C');
  test_now += 500;
  installer.receive("920000");
  test_now += 500;
  installer.tick();
  CHECK(installer.writes.size() == 1);
  #endif
  std::cout << "PASS: runtime initialization toggle, ACK isolation, installer authorization expiry during preamble\n";
}

void runtime_temperature_limit_tests() {
  TestHub hub;
  hub.setup();
  CHECK(hub.change_temperature_limit(true, 12));
  CHECK(hub.change_temperature_limit(false, 30));
  for (float value : {9.0f, 36.0f, 12.5f, NAN, INFINITY}) {
    CHECK(!hub.change_temperature_limit(true, value));
    CHECK(!hub.change_temperature_limit(false, value));
  }
  CHECK(!hub.change_temperature_limit(true, 30));
  CHECK(!hub.change_temperature_limit(false, 12));
  CHECK(hub.min_temperature() == 12 && hub.max_temperature() == 30);
  CHECK(hub.writes.empty());
  hub.synchronize();
  CHECK(!hub.request_target(11));
  CHECK(hub.request_target(29));
  CHECK(hub.change_temperature_limit(false, 28));
  hub.tick();
  CHECK(hub.writes.empty());
  std::cout << "PASS: runtime temperature limit validation, no stove writes, queued target recheck\n";
}

class TestTemperatureLimit : public DuepiTemperatureLimit {
 public:
  void change(float value) { this->control(value); }
};

void temperature_limit_entity_tests() {
  TestHub hub;
  TestTemperatureLimit minimum;
  TestTemperatureLimit maximum;
  minimum.set_parent(&hub);
  minimum.set_minimum(true);
  maximum.set_parent(&hub);
  hub.set_temperature_limit_number(true, &minimum);
  hub.set_temperature_limit_number(false, &maximum);
  hub.setup();
  CHECK(minimum.state == 10 && maximum.state == 25);
  minimum.change(12);
  maximum.change(30);
  CHECK(minimum.state == 12 && maximum.state == 30);
  minimum.change(30);
  maximum.change(11);
  maximum.change(NAN);
  CHECK(minimum.state == 12 && maximum.state == 30);
  CHECK(hub.writes.empty());

  TestHub rebooted;
  TestTemperatureLimit restored_minimum;
  TestTemperatureLimit restored_maximum;
  rebooted.set_temperature_limit_number(true, &restored_minimum);
  rebooted.set_temperature_limit_number(false, &restored_maximum);
  rebooted.setup();
  CHECK(rebooted.min_temperature() == 12 && rebooted.max_temperature() == 30);
  CHECK(restored_minimum.state == 12 && restored_maximum.state == 30);
  CHECK(rebooted.writes.empty());

  struct InvalidLimits { uint16_t minimum; uint16_t maximum; } invalid{30, 12};
  auto pref = minimum.make_entity_preference<InvalidLimits>(0xD0E71001);
  CHECK(pref.save(&invalid));
  TestHub corrupted;
  corrupted.set_min_temperature(11);
  corrupted.set_max_temperature(26);
  corrupted.set_temperature_limit_number(true, &restored_minimum);
  corrupted.setup();
  CHECK(corrupted.min_temperature() == 11 && corrupted.max_temperature() == 26);
  CHECK(restored_minimum.state == 11);
  std::cout << "PASS: limit configuration entities, rejected state rollback, paired restore, invalid saved defaults\n";
}

size_t count_logs(const char *level, const char *message) {
  size_t count = 0;
  for (const auto &entry : test_logs)
    if (entry.level == level && entry.message.find(message) != std::string::npos) ++count;
  return count;
}

void logging_tests() {
  test_logs.clear();
  test_log_level = 3;
  test_now = 0;
  TestHub hub;
  hub.setup();
  hub.publish();
  CHECK(count_logs("INFO", "Waiting for fresh") == 1);
  hub.synchronize();
  hub.publish();
  CHECK(count_logs("INFO", "Communication healthy") == 1);
  CHECK(count_logs("INFO", "Controller firmware: 9200") == 1);
  size_t initial_info = count_logs("INFO", "");
  hub.observe(Command::FIRMWARE, 0x920000);
  hub.observe(Command::PHASE, 0);
  hub.observe(Command::ALARM, 0);
  hub.observe(Command::ROOM, 0x00D800);
  CHECK(count_logs("INFO", "") == initial_info);
  CHECK(hub.request_target(20));
  hub.tick();
  CHECK(count_logs("INFO", "set_target #1: sent") == 1);
  hub.receive("000000");
  CHECK(count_logs("INFO", "set_target #1: acknowledged, unverified") == 1);
  CHECK(count_logs("INFO", "verified by readback") == 0);
  hub.observe(Command::TARGET, 0x001400);
  CHECK(count_logs("INFO", "set_target #1: verified by readback") == 1);
  test_now = 45001;
  hub.publish();
  hub.publish();
  CHECK(count_logs("WARN", "Communication lost") == 1);
  hub.synchronize();
  CHECK(count_logs("INFO", "Communication healthy") == 2);
  hub.report({Command::ROOM}, Outcome::TIMED_OUT);
  hub.report({Command::ROOM}, Outcome::TIMED_OUT);
  CHECK(count_logs("WARN", "No response to room") == 1);
  test_now += 30000;
  hub.report({Command::ROOM}, Outcome::TIMED_OUT);
  CHECK(count_logs("WARN", "No response to room") == 2);
  hub.report({Command::RESET, 0, 9}, Outcome::INDETERMINATE);
  CHECK(count_logs("WARN", "reset #9: indeterminate: not retried") == 1);
  hub.reject_control("Repeated rejection");
  hub.reject_control("Repeated rejection");
  CHECK(count_logs("WARN", "Repeated rejection") == 1);
  hub.reject_control("Distinct rejection");
  CHECK(count_logs("WARN", "Distinct rejection") == 1);
  test_now += 30000;
  hub.reject_control("Distinct rejection");
  CHECK(count_logs("WARN", "Distinct rejection") == 2);
  CHECK(count_logs("DEBUG", "") == 0 && count_logs("VERBOSE", "") == 0);
  CHECK(count_logs("INFO", "payload=") == 0 && count_logs("INFO", "TX frame") == 0);
  CHECK(hub.writes.size() == 1);
  CHECK(std::string(command_name(static_cast<Command>(255))) == "unknown");
  hub.observe(Command::FIRMWARE, 0xE69300);
  hub.observe(Command::FIRMWARE, 0xE69300);
  CHECK(count_logs("INFO", "Controller firmware: E693") == 1);
  std::cout << "PASS: named command audit, ACK/readback distinction, health transitions, rate-limited timeouts\n";
}

void detailed_logging_tests() {
  test_log_level = 6;
  test_logs.clear();
  test_now = 0;
  TestHub unavailable;
  unavailable.setup();
  unavailable.loop();
  test_now = 800;
  unavailable.loop();
  CHECK(count_logs("DEBUG", "firmware #0: response timeout") == 1);
  CHECK(count_logs("DEBUG", "read retry scheduled") == 1);
  CHECK(count_logs("WARN", "No response") == 0);
  test_now = 1050;
  unavailable.loop();
  CHECK(count_logs("DEBUG", "quiet recovery complete") == 1);
  test_now = 1850;
  unavailable.loop();
  CHECK(count_logs("WARN", "No response to firmware") == 1);
  CHECK(unavailable.writes.size() == 2);
  CHECK(count_logs("VERBOSE", "TX frame") == 2);

  test_now = 0;
  TestHub parser;
  parser.setup();
  parser.loop();
  parser.input = {0x1B, '0', '&'};
  test_now = 100;
  parser.loop();
  test_now = 1000;
  parser.loop();
  CHECK(count_logs("DEBUG", "Parser rejected/expired frames: +1, total 1") == 1);
  parser.loop();
  CHECK(count_logs("DEBUG", "Parser rejected/expired frames: +1, total 1") == 1);

  #ifdef USE_DUEPI_INSTALLER
  TestHub installer;
  prepare_installer(installer);
  CHECK(installer.arm_installer_write());
  CHECK(installer.installer_write(3, 42));
  installer.tick();
  installer.receive("010000");
  CHECK(count_logs("DEBUG", "framed response outside expected domain; ignored") == 1);
  installer.receive("000000");
  test_now += 100;
  installer.tick();
  installer.receive("2B0000");
  CHECK(count_logs("WARN", "Installer PR3 #1 readback mismatch: requested 42, received 43") == 1);
  CHECK(count_logs("VERBOSE", "accepted payload=2B0000") == 1);
  CHECK(count_logs("INFO", "Installer #1: PR3 readback 43: MISMATCH, write unverified") == 1);
  CHECK(installer.writes.size() == 2);
  #endif
  std::cout << "PASS: quiet INFO polling, DEBUG retries/recovery/parser summaries, VERBOSE frames, mismatch warning\n";
}

void shutdown_tests() {
  for (uint16_t phase : {0x0301, 0x0801, 0x0802}) {
    for (bool initialization : {false, true}) {
      test_now = 0;
      TestHub hub;
      text_sensor::TextSensor status;
      hub.set_text_sensor(4, &status);
      hub.setup();
      hub.set_enable_reset(true);
      hub.set_init_command(initialization);
      hub.synchronize();
      hub.observe(Command::PHASE, 0x020100);
      CHECK(hub.request_target(22));
      CHECK(hub.request_level(3));
      CHECK(hub.request_reset());
      CHECK(hub.queue({Command::ROOM}));
      if (initialization) hub.tick();
      hub.observe(Command::PHASE, static_cast<uint32_t>(phase) << 8);
      CHECK(hub.queued() == 1);
      CHECK(status.state.find("cancelled") != std::string::npos);
      CHECK(!hub.request_start());
      CHECK(!hub.request_target(23));
      CHECK(!hub.request_level(2));
      CHECK(!hub.request_reset());
      CHECK(!hub.request_off());
      if (initialization) hub.receive("920000");
      test_now += 100;
      hub.tick();
      if (initialization) {
        hub.receive("920000");
        test_now += 100;
        hub.tick();
      }
      CHECK(std::string(hub.writes.back().begin() + 1, hub.writes.back().begin() + 7) == "RD1000");
      hub.receive("00D700");
      CHECK(hub.queued() == 0);
      size_t transmitted = hub.writes.size();
      hub.observe(Command::PHASE, 0);
      test_now += 100;
      hub.tick();
      CHECK(hub.writes.size() == transmitted);
      CHECK(hub.request_target(23));
    }
  }
  test_now = 0;
  TestHub redundant;
  text_sensor::TextSensor status;
  redundant.set_text_sensor(4, &status);
  redundant.setup();
  redundant.synchronize();
  redundant.observe(Command::PHASE, 0x020100);
  CHECK(redundant.request_off());
  CHECK(!redundant.request_target(22));
  redundant.observe(Command::PHASE, 0x080100);
  CHECK(redundant.queued() == 0);
  CHECK(status.state.find("cancelled") != std::string::npos);
  redundant.tick();
  CHECK(redundant.writes.empty());
  redundant.observe(Command::PHASE, 0);
  redundant.tick();
  CHECK(redundant.writes.empty());

  TestHub send_time;
  send_time.set_text_sensor(4, &status);
  send_time.setup();
  send_time.synchronize();
  CHECK(send_time.request_target(22));
  send_time.cache_phase(0x0301);
  send_time.tick();
  CHECK(send_time.writes.empty());
  CHECK(status.state.find("rejected") != std::string::npos);

  for (uint16_t phase : {0x1000, 0x1001, 0x1002, 0x1003}) {
    test_now = 0;
    TestHub eco;
    eco.setup();
    eco.synchronize();
    CHECK(eco.request_target(22));
    eco.observe(Command::PHASE, static_cast<uint32_t>(phase) << 8);
    CHECK(eco.queued() == 1);
    CHECK(!eco.request_start());
    CHECK(!eco.request_level(3));
    eco.tick();
    CHECK(std::string(eco.writes.back().begin() + 1, eco.writes.back().begin() + 7) == "RF2160");
    eco.receive("000000");
    CHECK(eco.request_off());
    CHECK(!eco.request_target(23));
    test_now += 100;
    eco.tick();
    CHECK(std::string(eco.writes.back().begin() + 1, eco.writes.back().begin() + 7) == "RF0000");
    CHECK(!eco.request_target(23));
  }
#ifdef USE_DUEPI_INSTALLER
  for (bool initialization : {false, true}) {
    test_now = 0;
    TestHub pending_off;
    prepare_installer(pending_off);
    pending_off.set_enable_custom_commands(true);
    CHECK(pending_off.set_installer_access(true));
    pending_off.set_init_command(initialization);
    CHECK(pending_off.request_off());
    CHECK(!pending_off.request_custom("D100", false));
    CHECK(pending_off.request_installer_write(3, 42));
    pending_off.tick();
    CHECK(!pending_off.request_custom("D100", false));
    if (initialization) {
      pending_off.receive("920000");
      test_now += 100;
      pending_off.tick();
      CHECK(!pending_off.request_custom("D100", false));
    }
    CHECK(std::string(pending_off.writes.back().begin() + 1, pending_off.writes.back().begin() + 7) == "RF0000");
  }
  for (Command command : {Command::CUSTOM}) {
    for (bool send_time_only : {false, true}) {
      test_now = 0;
      TestHub advanced;
      text_sensor::TextSensor custom_response;
      advanced.set_text_sensor(6, &custom_response);
      prepare_installer(advanced);
      advanced.set_enable_custom_commands(true);
      advanced.set_enable_raw_commands(true);
      CHECK(advanced.set_installer_access(true));
      if (command == Command::CUSTOM) CHECK(advanced.request_custom("D100", false));
      else CHECK(advanced.request_installer_write(3, 42));
      if (send_time_only) {
        advanced.cache_phase(0x0802);
        advanced.tick();
        CHECK(advanced.writes.empty());
      } else {
        CHECK(advanced.installer_read(7));
        advanced.observe(Command::PHASE, 0x080200);
        CHECK(advanced.queued() == 1);
        advanced.tick();
        CHECK(std::string(advanced.writes.back().begin() + 1, advanced.writes.back().begin() + 7) == "L07000");
        advanced.receive("2A0000");
      }
      if (command == Command::CUSTOM) CHECK(custom_response.state == "Unavailable");
      CHECK(!advanced.request_custom("D100", false));
      CHECK(!advanced.request_custom("RD1000", true));
      advanced.observe(Command::PHASE, 0);
      size_t transmitted = advanced.writes.size();
      test_now += 100;
      advanced.tick();
      CHECK(advanced.writes.size() == transmitted);
    }
  }
  for (uint16_t phase : {0x0000, 0x0200, 0x0201, 0x0301, 0x0401, 0x0801, 0x0802, 0x1000, 0x1003,
                         0x0101, 0x0102, 0x0103, 0x0104, 0x0105, 0x010A, 0x0132, 0x0133, 0x9999}) {
    for (bool send_time : {false, true}) {
      test_now = 0;
      TestHub parameter;
      prepare_installer(parameter);
      bool permitted = known_phase(phase) && (phase >> 8) != 1;
      if (send_time) {
        CHECK(parameter.request_installer_write(28, 1));
        parameter.observe(Command::PHASE, static_cast<uint32_t>(phase) << 8);
      } else {
        parameter.observe(Command::PHASE, static_cast<uint32_t>(phase) << 8);
        CHECK(parameter.request_installer_write(28, 1) == permitted);
      }
      parameter.tick();
      CHECK(parameter.writes.size() == (permitted ? 1U : 0U));
    }
  }
#endif
  test_now = 0;
  std::cout << "PASS: observed shutdown cancels queued/held writes, preserves reads, blocks redundant OFF and advanced bypasses; eco target/OFF remain available\n";
}

void deferred_off_tests() {
  for (bool initialization : {false, true}) {
    test_now = 0;
    TestHub hub;
    hub.setup();
    hub.set_init_command(initialization);
    hub.synchronize();
    CHECK(hub.request_start());
    hub.tick();
    if (initialization) {
      hub.receive("920000");
      test_now += 100;
      hub.tick();
    }
    CHECK(hub.request_target(20));
    CHECK(hub.request_off());
    CHECK(!hub.request_target(21));
    hub.receive("000000");
    test_now += 100;
    hub.tick();
    if (initialization) {
      hub.receive("920000");
      test_now += 100;
      hub.tick();
    }
    for (const char *payload : {"920000", "000000", "020100", "00D700", "000100"}) {
      test_now += 100;
      hub.loop();
      if (std::string(payload) == "920000") {
        CHECK(std::string(hub.writes.back().begin() + 1, hub.writes.back().begin() + 7) == "RDC000");
      } else if (initialization) {
        hub.receive("920000");
        test_now += 100;
        hub.loop();
      }
      hub.receive(payload);
    }
    test_now += 100;
    hub.loop();
    if (initialization) {
      hub.receive("920000");
      test_now += 100;
      hub.loop();
    }
    CHECK(std::string(hub.writes.back().begin() + 1, hub.writes.back().begin() + 7) == "RF0000");
    hub.receive("000000");
    size_t off_frames = 0;
    for (const auto &frame : hub.writes) {
      std::string payload(frame.begin() + 1, frame.begin() + 7);
      CHECK(payload != "RF2140" && payload != "RF2150");
      if (payload == "RF0000") ++off_frames;
    }
    CHECK(off_frames == 1);
  }
  for (bool timeout : {false, true}) {
    test_now = 0;
    TestHub hub;
    text_sensor::TextSensor status;
    hub.set_text_sensor(4, &status);
    hub.setup();
    hub.synchronize();
    CHECK(hub.request_start());
    hub.tick();
    CHECK(hub.request_off());
    hub.receive("000000");
    if (timeout) {
      test_now = 100;
      hub.loop();
      test_now = 900;
      hub.tick();
    } else {
      test_now = 5000;
      hub.tick();
    }
    CHECK(status.state.find("cancelled") != std::string::npos);
    test_now += 250;
    hub.tick();
    if (timeout) hub.receive("920000");
    hub.synchronize();
    test_now += 100;
    hub.tick();
    for (const auto &frame : hub.writes)
      CHECK(std::string(frame.begin() + 1, frame.begin() + 7) != "RF0000");
  }
  test_now = 0;
  std::cout << "PASS: queued OFF survives ignition ACK, permits prerequisite polling, expires and cancels on recovery\n";
}

#ifdef USE_DUEPI_INSTALLER
void installer_parameter_range_tests() {
  for (int parameter : {0, 1, 28, 29, 255}) {
    for (unsigned operation : {0U, 1U, 2U, 3U, 4U}) {
      test_now = 0;
      TestHub hub;
      TestStandbySwitch standby;
      hub.set_enable_installer(true);
      hub.set_enable_custom_commands(true);
      hub.set_enable_raw_commands(true);
      hub.set_installer_parameter_range(1, 28);
      hub.set_standby_mode(&standby, parameter);
      hub.setup();
      hub.synchronize();
      CHECK(hub.set_installer_access(true));
      bool accepted = false;
      if (operation == 0) accepted = hub.installer_read(parameter);
      if (operation == 1) accepted = hub.request_installer_write(parameter, 255);
      if (operation == 2 || operation == 3) {
        const char *hex = "0123456789ABCDEF";
        char payload[] = {operation == 2 ? 'L' : 'S', hex[parameter >> 4], hex[parameter & 15],
                          operation == 2 ? '0' : 'F', operation == 2 ? '0' : 'F', '0', 0};
        accepted = hub.request_custom(payload, true);
      }
      if (operation == 4) accepted = hub.request_standby(true);
      bool allowed = parameter >= 1 && parameter <= 28;
      CHECK(accepted == allowed);
      CHECK(hub.queued() == (allowed ? 1U : 0U));
      hub.tick();
      CHECK(hub.writes.size() == (allowed ? 1U : 0U));
    }
  }
  for (bool write : {false, true}) {
    test_now = 0;
    TestHub hub;
    hub.set_enable_installer(true);
    hub.set_installer_parameter_range(1, 28);
    hub.setup();
    hub.synchronize();
    CHECK(hub.set_installer_access(true));
    CHECK(write ? hub.request_installer_write(28, 255) : hub.installer_read(28));
    hub.set_installer_parameter_range(1, 27);
    hub.tick();
    CHECK(hub.writes.empty());
  }
  test_now = 0;
  TestHub hub;
  TestInstallerValue parameter;
  TestInstallerValue value;
  hub.set_installer_parameter_range(10, 20);
  hub.set_installer_number(false, &parameter);
  hub.set_installer_number(true, &value);
  hub.setup();
  CHECK(parameter.state == 10);
  CHECK(value.state == 0);
  std::cout << "PASS: installer parameter bounds cover reads, writes, raw commands, standby, boot and send-time checks\n";
}
#endif

void optional_diagnostic_tests() {
  for (unsigned mode : {0U, 1U, 2U, 3U}) {
    test_now = 0;
    TestHub hub;
    sensor::Sensor readings[4];
    text_sensor::TextSensor cycle;
    for (size_t index = 0; index < 4; ++index) {
      if (mode == 3 || (mode == 1 && index == 0))
        hub.set_numeric_sensor(static_cast<size_t>(Command::FLUE_EXTRACTION_SPEED) + index, &readings[index]);
    }
    if (mode == 2 || mode == 3) hub.set_text_sensor(9, &cycle);
    hub.setup();
    std::vector<std::string> expected = {
        "RDC000", "RC4000", "RD9000", "RD1000", "RDA000", "RC5000", "RC6000",
        "RD3000", "RD4000", "RD0000", "REF000", "RC0000", "RDF000"};
    if (mode == 1 || mode == 3) expected.push_back("RD2000");
    if (mode == 3) {
      expected.push_back("RDB000");
      expected.push_back("RDD000");
      expected.push_back("RDE000");
    }
    if (mode == 2 || mode == 3) expected.push_back("RD8000");
    expected.push_back("RED000");
    expected.push_back("REE000");
    for (unsigned pass : {0U, 1U}) {
      if (pass == 1) test_now = 10000;
      size_t before = hub.writes.size();
      size_t count = expected.size() - (pass == 0 ? 0 : 2);
      for (size_t index = 0; index < count; ++index) {
        hub.poll();
        CHECK(hub.writes.size() == before + index + 1);
        CHECK(std::string(hub.writes.back().begin() + 1, hub.writes.back().begin() + 7) == expected[index]);
        const auto &command = expected[index];
        const char *reply = command == "RDC000" ? "A06000" : command == "RD1000" ? "00D700" :
                            command == "RC5000" ? "000300" : command == "RC6000" ? "001500" :
                            command == "RD2000" ? "123400" : command == "RDB000" ? "020100" :
                            command == "RDD000" ? "FE3300" : command == "RDE000" ? "7F4400" :
                            command == "RD8000" ? "0A0B00" : "000000";
        hub.receive(reply);
        test_now += 100;
      }
      hub.poll();
      CHECK(hub.writes.size() == before + count);
    }
    if (mode == 1 || mode == 3) CHECK(readings[0].state == 4660);
    if (mode == 3) {
      CHECK(readings[1].state == 513);
      CHECK(readings[2].state == 254);
      CHECK(readings[3].state == 127);
    }
    if (mode == 2 || mode == 3) {
      CHECK(cycle.state == "10:11");
      hub.observe(Command::CYCLE_TIME, 0x010200);
      CHECK(cycle.state == "01:02");
      hub.observe(Command::CYCLE_TIME, 0xFFFE00);
      CHECK(cycle.state == "255:254");
    }
    test_now = 60000;
    hub.publish();
    if (mode == 1 || mode == 3) CHECK(std::isnan(readings[0].state));
    if (mode == 3)
      for (const auto &reading : readings) CHECK(std::isnan(reading.state));
    if (mode == 2 || mode == 3) CHECK(cycle.state == "Stale");
  }
  test_now = 0;
  std::cout << "PASS: optional diagnostic polling, byte/word decoding, cycle formatting, freshness and runtime ordering\n";
}

int main() {
#ifdef USE_DUEPI_INSTALLER
  installer_parameter_range_tests();
#endif
  optional_diagnostic_tests();
  power_preset_tests();
  power_feedback_tests();
  shutdown_tests();
  deferred_off_tests();
  normal_tests();
  target_feedback_tests();
#ifdef USE_DUEPI_INSTALLER
  standby_tests();
  installer_tests();
  installer_readback_failure_tests();
  installer_disabled_before_ack_test();
  custom_response_failure_tests();
  custom_tests();
#else
  WireFrame frame{};
  CHECK(!build_request({Command::INSTALLER_READ}, frame));
  CHECK(!build_request({Command::INSTALLER_WRITE}, frame));
  CHECK(!build_request({Command::CUSTOM}, frame));
#endif
  verification_expiry_test();
  temperature_limit_tests();
  initialization_hub_tests();
  runtime_temperature_limit_tests();
  temperature_limit_entity_tests();
  logging_tests();
  detailed_logging_tests();
}