#include "protocol.h"
#include "transaction.h"
#include "state.h"
#include "control.h"
#include "privilege.h"

#include <cstdlib>
#include <iostream>
#include <string>
#include <vector>

using namespace esphome::duepi;

#define CHECK(condition) do { if (!(condition)) { std::cerr << __LINE__ << ": " << #condition << '\n'; std::exit(1); } } while (false)

struct Event { Request request; Outcome outcome; uint32_t data; };

struct FakeIO : TransactionIO {
  std::vector<WireFrame> writes;
  std::vector<Event> events;
  void transmit(const WireFrame &frame) override { writes.push_back(frame); }
  void result(const Request &request, Outcome outcome, const Response *response) override {
    events.push_back({request, outcome, response == nullptr ? 0U : response->data});
  }
  size_t count(Outcome outcome) const {
    size_t matches = 0;
    for (const auto &event : events) if (event.outcome == outcome) ++matches;
    return matches;
  }
};

WireFrame synthetic(const char *data) {
  WireFrame frame{};
  CHECK(encode_payload(data, 6, frame));
  return frame;
}

void reply(TransactionEngine &engine, const char *data, uint32_t now) {
  for (uint8_t byte : synthetic(data)) engine.receive(byte, now);
}

void codec_tests() {
  WireFrame frame{};
  CHECK(encode_command("D100", 4, frame));
  CHECK(std::string(frame.begin() + 1, frame.end()) == "RD100057&");
  CHECK(encode_command("d900", 4, frame));
  CHECK(std::string(frame.begin() + 1, frame.end()) == "RD90005F&");
  CHECK(encode_payload("RZZZZR", 6, frame));
  CHECK(frame[7] == '0' && frame[8] == 'C');
  CHECK(!encode_command("G100", 4, frame));
  CHECK(!encode_command("D1000", 5, frame));
  CHECK(!encode_command(nullptr, 4, frame));
  CHECK(!encode_payload("R&0000", 6, frame));
  Response response{};
  frame = synthetic("00D700");
  CHECK(frame[7] == '3' && frame[8] == 'B');
  CHECK(decode_response(frame, response));
  CHECK(response.word() == 215);
  CHECK(response.data == 0x00D700);
  frame[7] = '0';
  CHECK(!decode_response(frame, response));
  frame = synthetic("00G700");
  CHECK(!decode_response(frame, response));
  frame = synthetic("00D700");
  frame[0] = 'X';
  CHECK(!decode_response(frame, response));
  frame = synthetic("00D700");
  frame[9] = 'X';
  CHECK(!decode_response(frame, response));
  CHECK(decode_response(synthetic("ABCDEF"), response));
  CHECK(response.data == 0xABCDEF);
  CHECK(!build_request({Command::SET_TARGET, 9}, frame));
  CHECK(!build_request({Command::SET_TARGET, 36}, frame));
  CHECK(!build_request({Command::SET_LEVEL, 256}, frame));
  CHECK(build_request({Command::SET_TARGET, 21}, frame));
  CHECK(std::string(frame.begin() + 1, frame.begin() + 7) == "RF2150");
  CHECK(build_request({Command::START_GENERIC}, frame));
  CHECK(std::string(frame.begin() + 1, frame.end()) == "RF001059&");
  CHECK(is_write(Command::START_GENERIC));
  CHECK(std::string(command_name(Command::START_GENERIC)) == "start_generic");
  CHECK(accepts({Command::START_GENERIC}, Response{0}));
  CHECK(!accepts({Command::START_GENERIC}, Response{1}));
  CHECK(accepts({Command::TARGET}, Response{0x001500}));
  CHECK(accepts({Command::TARGET}, Response{0xFFFF00}));
  for (const char *payload : {"000000", "000900", "002400", "FFFF00"}) {
    FakeIO feedback;
    TransactionEngine engine(feedback);
    CHECK(engine.enqueue({Command::TARGET}, 0));
    engine.tick(0);
    reply(engine, payload, 1);
    CHECK(feedback.events.back().request.command == Command::TARGET);
    CHECK(feedback.events.back().outcome == Outcome::ACKNOWLEDGED);
    CHECK(engine.enqueue({Command::ROOM}, 100));
    engine.tick(101);
    CHECK(feedback.writes.size() == 2);
    CHECK(feedback.events.back().request.command == Command::ROOM);
    CHECK(feedback.events.back().outcome == Outcome::SENT);
  }
  CHECK(!accepts({Command::OFF}, Response{0x000001}));
  CHECK(known_phase(0x0201));
  CHECK(!known_phase(0xDEAD));
}

void parser_tests() {
  FrameParser parser;
  Response response{};
  for (size_t index = 0; index < 1000; ++index) CHECK(!parser.feed('?', 0, response));
  auto frame = synthetic("00D700");
  for (size_t index = 0; index < 5; ++index) CHECK(!parser.feed(frame[index], 10, response));
  CHECK(parser.buffered() == 5);
  for (size_t index = 5; index < 9; ++index) CHECK(!parser.feed(frame[index], 30, response));
  CHECK(parser.feed(frame[9], 30, response));
  CHECK(response.word() == 215);
  size_t frames = 0;
  for (size_t repeat = 0; repeat < 3; ++repeat)
    for (auto byte : frame) if (parser.feed(byte, 40, response)) ++frames;
  CHECK(frames == 3);
  parser.feed(0x1B, 50, response);
  parser.feed('0', 50, response);
  parser.expire(100);
  CHECK(parser.buffered() == 0);
  parser.feed(0x1B, 110, response);
  for (size_t index = 0; index < 1000; ++index) CHECK(!parser.feed('0', 110, response));
  CHECK(parser.buffered() == 0);
  for (auto byte : frame) if (parser.feed(byte, 111, response)) ++frames;
  CHECK(frames == 4);
  parser.feed(0x1B, 120, response);
  parser.feed('&', 120, response);
  CHECK(parser.buffered() == 0);
  parser.feed(0x1B, 130, response);
  parser.feed('0', 130, response);
  for (auto byte : frame) if (parser.feed(byte, 131, response)) ++frames;
  CHECK(frames == 5);
  parser.feed(0x1B, 0xFFFFFFF0U, response);
  parser.expire(35);
  CHECK(parser.buffered() == 0);
  CHECK(parser.errors() >= 5);
}

void transaction_tests() {
  FakeIO io;
  TransactionEngine engine(io);
  CHECK(engine.enqueue({Command::ROOM}, 0));
  CHECK(engine.enqueue({Command::ROOM}, 0));
  CHECK(engine.queued() == 1);
  engine.tick(0);
  CHECK(io.writes.size() == 1);
  engine.tick(799);
  CHECK(io.writes.size() == 1);
  engine.tick(800);
  CHECK(engine.recovering());
  reply(engine, "00D700", 900);
  CHECK(io.count(Outcome::ACKNOWLEDGED) == 0);
  engine.tick(1149);
  CHECK(io.writes.size() == 1);
  engine.tick(1150);
  CHECK(io.writes.size() == 2);
  engine.tick(1950);
  CHECK(io.count(Outcome::TIMED_OUT) == 1);
  engine.tick(2200);
  engine.tick(10000);
  CHECK(io.writes.size() == 2);
  CHECK(engine.enqueue({Command::ROOM}, 10000));
  engine.tick(10000);
  reply(engine, "FFFF00", 10010);
  CHECK(io.count(Outcome::ACKNOWLEDGED) == 0);
  reply(engine, "00D700", 10020);
  CHECK(io.count(Outcome::ACKNOWLEDGED) == 1);
  CHECK(io.events.back().data == 0x00D700);
  reply(engine, "00D700", 10030);
  CHECK(io.count(Outcome::ACKNOWLEDGED) == 1);
}

void queue_tests() {
  FakeIO io;
  TransactionEngine engine(io);
  CHECK(engine.enqueue({Command::ROOM}, 0));
  CHECK(engine.enqueue({Command::PHASE}, 0));
  CHECK(engine.enqueue({Command::ALARM}, 0));
  CHECK(engine.enqueue({Command::TARGET}, 0));
  CHECK(!engine.enqueue({Command::PCB}, 0));
  CHECK(engine.enqueue({Command::START_9200, 0, 1}, 0));
  CHECK(engine.enqueue({Command::SET_LEVEL, 3, 2}, 0));
  CHECK(engine.enqueue({Command::SET_TARGET, 20, 3}, 0));
  CHECK(engine.enqueue({Command::SET_TARGET, 24, 4}, 0));
  CHECK(engine.queued() == 7);
  CHECK(engine.enqueue({Command::OFF, 0, 5}, 0));
  CHECK(engine.queued() == 5);
  engine.tick(0);
  CHECK(std::string(io.writes[0].begin() + 1, io.writes[0].begin() + 7) == "RF0000");
  CHECK(io.count(Outcome::CANCELLED) == 4);
  reply(engine, "000000", 1);
  CHECK(io.count(Outcome::ACKNOWLEDGED) == 1);
  engine.tick(100);
  CHECK(io.writes.size() == 1);
  engine.tick(101);
  CHECK(io.writes.size() == 2);
  engine.resynchronize(102);
  CHECK(engine.queued() == 0);
  CHECK(!engine.enqueue({Command::START_9200}, 103));
  engine.tick(352);
  engine.tick(10000);
  CHECK(io.writes.size() == 2);

  FakeIO full_io;
  TransactionEngine full(full_io);
  for (size_t index = 0; index < 8; ++index) CHECK(full.enqueue({Command::START_9200}, 0));
  CHECK(!full.enqueue({Command::START_9200}, 0));
  CHECK(full.queued() == 8);
  full.tick(5000);
  CHECK(full_io.writes.empty());
  CHECK(full.queued() == 7);
}

void write_timeout_tests() {
  FakeIO io;
  TransactionEngine engine(io);
  engine.enqueue({Command::START_E693}, 0);
  engine.enqueue({Command::SET_LEVEL, 3}, 0);
  engine.tick(0);
  engine.tick(800);
  CHECK(io.count(Outcome::INDETERMINATE) == 1);
  CHECK(io.count(Outcome::CANCELLED) == 1);
  CHECK(engine.queued() == 0);
  reply(engine, "000000", 810);
  engine.tick(1060);
  engine.tick(100000);
  CHECK(io.writes.size() == 1);
  CHECK(io.count(Outcome::ACKNOWLEDGED) == 0);

  FakeIO deadline_io;
  TransactionEngine deadline(deadline_io);
  deadline.enqueue({Command::OFF}, 0);
  deadline.tick(0);
  reply(deadline, "000000", 800);
  CHECK(deadline_io.count(Outcome::INDETERMINATE) == 1);
  CHECK(deadline_io.count(Outcome::ACKNOWLEDGED) == 0);
}

void wrap_tests() {
  FakeIO io;
  TransactionEngine engine(io);
  uint32_t now = 0xFFFFFF00U;
  engine.enqueue({Command::ROOM}, now);
  engine.tick(now);
  engine.tick(now + 799U);
  CHECK(!engine.recovering());
  engine.tick(now + 800U);
  CHECK(engine.recovering());
  engine.tick(now + 1050U);
  CHECK(io.writes.size() == 2);
  reply(engine, "00D700", now + 1060U);
  CHECK(io.count(Outcome::ACKNOWLEDGED) == 1);
}

void state_tests() {
  ObservedState state;
  CHECK(!state.healthy(0, 45000));
  CHECK(!state.fresh(Command::ROOM, 0, 45000));
  CHECK(state.update(Command::PHASE, {0x020100}, 100));
  CHECK(state.update(Command::ROOM, {0x00D700}, 100));
  CHECK(!state.healthy(100, 45000));
  CHECK(state.update(Command::ALARM, {0}, 100));
  CHECK(state.healthy(100, 45000));
  CHECK(!state.healthy(45100, 45000));
  CHECK(state.update(Command::PCB, {0x001E00}, 45101));
  CHECK(!state.healthy(45101, 45000));
  CHECK(!state.update(Command::TARGET, {0xFFFF00}, 45102));
  CHECK(!state.fresh(Command::TARGET, 45102, 45000));
  CHECK(numeric_value(Command::ROOM, 0x00D700) == 21.5f);
  CHECK(numeric_value(Command::EXHAUST, 0x00C800) == 2000.0f);
  CHECK(numeric_value(Command::TOTAL_RUNTIME, 0xABCDEF) == 11259375.0f);
  state.invalidate();
  CHECK(!state.healthy(100, 45000));
  state.update(Command::ROOM, {0x00D700}, 0xFFFFFF00U);
  CHECK(state.fresh(Command::ROOM, 50, 45000));
}

void control_tests() {
  ObservedState state;
  ControlPolicy policy;
  Request request;
  CHECK(!policy.permits({Command::OFF}, state, 0, 45000));
  for (Command command : {Command::PHASE, Command::ROOM, Command::ALARM, Command::FIRMWARE, Command::BOARD}) {
    CHECK(state.update(command, {command == Command::FIRMWARE ? 0x920000U : 0U}, 100));
  }
  CHECK(policy.permits({Command::OFF}, state, 100, 45000));
  CHECK(!policy.permits({Command::START_GENERIC}, state, 100, 45000));
  CHECK(policy.start_request(state, request));
  CHECK(request.command == Command::START_9200);
  CHECK(policy.permits(request, state, 100, 45000));
  CHECK(!policy.permits({Command::START_E693}, state, 100, 45000));
  CHECK(!policy.permits({Command::START_GENERIC}, state, 100, 45000));
  state.update(Command::FIRMWARE, {0xE67000}, 100);
  CHECK(policy.start_request(state, request) && request.command == Command::START_E670);
  state.update(Command::FIRMWARE, {0xE69300}, 100);
  CHECK(policy.start_request(state, request) && request.command == Command::START_E693);
  state.update(Command::FIRMWARE, {0x123400}, 100);
  CHECK(policy.start_request(state, request) && request.command == Command::START_GENERIC);
  CHECK(policy.permits(request, state, 100, 45000));
  CHECK(!policy.permits(request, state, 45100, 45000));
  CHECK(!policy.permits({Command::START_9200}, state, 100, 45000));
  CHECK(policy.permits({Command::SET_TARGET, 21}, state, 100, 45000));
  CHECK(!policy.permits({Command::SET_TARGET, 26}, state, 100, 45000));
  CHECK(!policy.permits({Command::SET_LEVEL, 3}, state, 100, 45000));
  state.update(Command::PHASE, {0x020100}, 100);
  CHECK(policy.permits({Command::SET_LEVEL, 3}, state, 100, 45000));
  CHECK(!policy.permits(request, state, 100, 45000));
  state.update(Command::PHASE, {0}, 100);
  state.update(Command::ALARM, {0x000500}, 100);
  CHECK(!policy.permits(request, state, 100, 45000));
  state.update(Command::ALARM, {0}, 100);
  policy.profile = FirmwareProfile::FW_E693;
  CHECK(policy.start_request(state, request));
  CHECK(request.command == Command::START_E693);
  CHECK(!policy.permits({Command::START_GENERIC}, state, 100, 45000));
  CHECK(policy.permits(request, state, 100, 45000));
  CHECK(!policy.permits(request, state, 45100, 45000));
  CHECK(!policy.permits({Command::RESET}, state, 100, 45000));
  policy.reset_enabled = true;
  CHECK(policy.permits({Command::RESET}, state, 100, 45000));
  CHECK(ControlPolicy::target_request(21, request));
  CHECK(ControlPolicy::verified_by({Command::SET_TARGET, 21}, Command::TARGET, {0x001500}));
  CHECK(!ControlPolicy::verified_by({Command::SET_TARGET, 21}, Command::TARGET, {0x001600}));
  CHECK(!ControlPolicy::verified_by({Command::SET_TARGET, 21}, Command::D4000, {0x001500}));
  CHECK(ControlPolicy::verified_by({Command::OFF}, Command::PHASE, {0}));
  CHECK(ControlPolicy::verified_by({Command::START_E693}, Command::PHASE, {0x010100}));
  CHECK(ControlPolicy::verified_by({Command::START_GENERIC}, Command::PHASE, {0x010100}));
  CHECK(!ControlPolicy::verified_by({Command::START_GENERIC}, Command::PHASE, {0}));
  CHECK(!ControlPolicy::verified_by({Command::START_GENERIC}, Command::PHASE, {0xDEAD00}));
  CHECK(!ControlPolicy::verified_by({Command::START_GENERIC}, Command::TARGET, {0x010100}));
  CHECK(!ControlPolicy::verified_by({Command::SET_LEVEL, 3}, Command::D4000, {0x000300}));
  CHECK(!ControlPolicy::target_request(NAN, request));
  CHECK(!ControlPolicy::target_request(INFINITY, request));
  CHECK(!ControlPolicy::target_request(21.5f, request));
  CHECK(!ControlPolicy::target_request(36, request));
  CHECK(!policy.permits({Command::SET_LEVEL, 3}, state, 100, 45000));
  state.update(Command::PHASE, {0x020100}, 100);
  CHECK(policy.permits({Command::SET_LEVEL, 3}, state, 100, 45000));
  CHECK(!policy.permits({Command::START_E693}, state, 100, 45000));
  state.update(Command::ALARM, {0x000500}, 100);
  CHECK(!policy.permits({Command::SET_LEVEL, 3}, state, 100, 45000));
  CHECK(policy.permits({Command::OFF}, state, 100, 45000));
  state.update(Command::PHASE, {0xDEAD00}, 100);
  CHECK(!policy.permits({Command::OFF}, state, 100, 45000));

  struct GuardedIO : FakeIO {
    bool authorized{true};
    bool may_transmit(const Request &, uint32_t) override { return authorized; }
  } guarded;
  TransactionEngine engine(guarded);
  engine.enqueue({Command::SET_TARGET, 20, 1}, 100);
  engine.enqueue({Command::SET_TARGET, 23, 2}, 100);
  engine.enqueue({Command::SET_TARGET, 25, 3}, 100);
  CHECK(engine.queued() == 1);
  guarded.authorized = false;
  engine.tick(100);
  CHECK(guarded.writes.empty());
  CHECK(guarded.count(Outcome::REJECTED) == 1);
}

void privilege_tests() {
  OneShotPermission permission;
  CHECK(!permission.set_enabled(true));
  CHECK(!permission.arm(0));
  permission.configured = true;
  CHECK(permission.set_enabled(true));
  CHECK(permission.arm(0xFFFFFF00U));
  CHECK(permission.reserve(7, 0xFFFFFF10U));
  CHECK(!permission.reserve(8, 0xFFFFFF11U));
  CHECK(permission.permits(7, 0));
  CHECK(!permission.permits(8, 0));
  CHECK(permission.consume(7, 0));
  CHECK(!permission.consume(7, 1));
  CHECK(permission.arm(100));
  CHECK(permission.reserve(9, 200));
  CHECK(!permission.permits(9, 30100));
  CHECK(permission.arm(40000));
  permission.set_enabled(false);
  CHECK(!permission.reserve(10, 40001));
  permission.set_enabled(true);
  CHECK(!permission.reserve(10, 40002));
  CHECK(permission.arm(50000));
  CHECK(permission.reserve(11, 50001));
  permission.disarm();
  CHECK(!permission.permits(11, 50002));
  Request request;
  WireFrame frame{};
  request.command = Command::INSTALLER_READ;
  request.parameter = 107;
  CHECK(build_request(request, frame));
  CHECK(std::string(frame.begin() + 1, frame.begin() + 7) == "L6B000");
  request.command = Command::INSTALLER_WRITE;
  request.argument = 255;
  CHECK(build_request(request, frame));
  CHECK(std::string(frame.begin() + 1, frame.begin() + 7) == "S6BFF0");
  request.argument = 256;
  CHECK(!build_request(request, frame));
  request.argument = 0;
  request.parameter = 256;
  CHECK(!build_request(request, frame));
  CHECK(custom_request("D100", false, request));
  CHECK(build_request(request, frame));
  CHECK(std::string(frame.begin() + 1, frame.end()) == "RD100057&");
  CHECK(custom_request("L01000", true, request));
  CHECK(!custom_request("L01&00", true, request));
  CHECK(!custom_request("L010000", true, request));
  CHECK(!custom_request("d100", true, request));
  CHECK(!custom_request("D10G", false, request));
  CHECK(!custom_request(std::string(10000, 'A'), true, request));
  FakeIO io;
  TransactionEngine engine(io);
  CHECK(custom_request("D100", false, request));
  CHECK(engine.enqueue(request, 0));
  engine.tick(0);
  engine.tick(800);
  engine.tick(1050);
  CHECK(io.writes.size() == 1);
  CHECK(io.count(Outcome::INDETERMINATE) == 1);
}

void initialization_tests() {
  FakeIO io;
  TransactionEngine engine(io);
  engine.init_command = true;
  CHECK(engine.enqueue({Command::SET_TARGET, 20, 1}, 0));
  engine.tick(0);
  CHECK(io.writes.size() == 1);
  CHECK(std::string(io.writes.back().begin() + 1, io.writes.back().begin() + 7) == "RDC000");
  CHECK(io.events.back().request.command == Command::FIRMWARE);
  CHECK(!engine.idle());
  CHECK(engine.enqueue({Command::SET_TARGET, 21, 2}, 10));
  reply(engine, "920000", 20);
  CHECK(!engine.idle());
  engine.tick(119);
  CHECK(io.writes.size() == 1);
  engine.tick(120);
  CHECK(io.writes.size() == 2);
  CHECK(std::string(io.writes.back().begin() + 1, io.writes.back().begin() + 7) == "RF2150");
  CHECK(io.events.back().request.token == 2);
  reply(engine, "000000", 121);
  CHECK(engine.idle());

  FakeIO timeout_io;
  TransactionEngine timed_out(timeout_io);
  timed_out.init_command = true;
  CHECK(timed_out.enqueue({Command::START_9200}, 0));
  timed_out.tick(0);
  timed_out.tick(800);
  CHECK(timeout_io.count(Outcome::CANCELLED) == 1);
  reply(timed_out, "920000", 900);
  timed_out.tick(1150);
  CHECK(timeout_io.writes.size() == 2);
  CHECK(timeout_io.events.back().request.command == Command::FIRMWARE);
  reply(timed_out, "920000", 1151);
  timed_out.tick(1300);
  CHECK(timeout_io.writes.size() == 2);

  FakeIO off_io;
  TransactionEngine off(off_io);
  off.init_command = true;
  CHECK(off.enqueue({Command::START_9200}, 0));
  off.tick(0);
  CHECK(off.enqueue({Command::OFF}, 1));
  CHECK(off_io.count(Outcome::CANCELLED) == 1);
  reply(off, "920000", 2);
  off.tick(102);
  reply(off, "920000", 103);
  off.tick(203);
  CHECK(off_io.writes.size() == 3);
  CHECK(std::string(off_io.writes.back().begin() + 1, off_io.writes.back().begin() + 7) == "RF0000");
  std::cout << "PASS: initialization reply isolation, spacing, coalescing, timeout cancellation, off ordering\n";
}

int main() {
  initialization_tests();
  codec_tests();
  parser_tests();
  transaction_tests();
  queue_tests();
  write_timeout_tests();
  wrap_tests();
  state_tests();
  control_tests();
  privilege_tests();
  std::cout << "PASS: codec, parser, transactions, queue, timeouts, clock wrap, freshness, controls, privileges\n";
}