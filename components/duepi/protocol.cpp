#include "protocol.h"

namespace esphome {
namespace duepi {

namespace {
constexpr char HEX[] = "0123456789ABCDEF";

int hex_digit(uint8_t character) {
  if (character >= '0' && character <= '9') return character - '0';
  if (character >= 'A' && character <= 'F') return character - 'A' + 10;
  if (character >= 'a' && character <= 'f') return character - 'a' + 10;
  return -1;
}
}  // namespace

bool encode_payload(const char *payload, size_t length, WireFrame &frame) {
  if (payload == nullptr || length != 6) return false;
  uint8_t sum = 0;
  WireFrame candidate{};
  candidate[0] = 0x1B;
  for (size_t index = 0; index < length; ++index) {
    auto character = static_cast<uint8_t>(payload[index]);
    if (character < 0x20 || character > 0x7E || character == '&') return false;
    candidate[index + 1] = character;
    sum = static_cast<uint8_t>(sum + character);
  }
  candidate[7] = HEX[sum >> 4];
  candidate[8] = HEX[sum & 15];
  candidate[9] = '&';
  frame = candidate;
  return true;
}

bool encode_command(const char *command, size_t length, WireFrame &frame) {
  if (command == nullptr || length != 4) return false;
  char payload[6] = {'R', 0, 0, 0, 0, '0'};
  for (size_t index = 0; index < length; ++index) {
    if (hex_digit(static_cast<uint8_t>(command[index])) < 0) return false;
    payload[index + 1] = HEX[hex_digit(static_cast<uint8_t>(command[index]))];
  }
  return encode_payload(payload, sizeof(payload), frame);
}

bool decode_response(const WireFrame &frame, Response &response) {
  if (frame[0] != 0x1B || frame[9] != '&') return false;
  uint8_t sum = 0;
  uint32_t value = 0;
  for (size_t index = 1; index <= 6; ++index) {
    int digit = hex_digit(frame[index]);
    if (digit < 0) return false;
    value = (value << 4) | static_cast<uint32_t>(digit);
    sum = static_cast<uint8_t>(sum + frame[index]);
  }
  int high = hex_digit(frame[7]);
  int low = hex_digit(frame[8]);
  if (high < 0 || low < 0 || sum != ((high << 4) | low)) return false;
  response.data = value;
  return true;
}

void FrameParser::expire(uint32_t now) {
  if (this->used_ != 0 && uint32_t(now - this->last_byte_) >= this->inter_byte_timeout) {
    this->clear();
    ++this->errors_;
  }
}

bool FrameParser::feed(uint8_t byte, uint32_t now, Response &response) {
  this->expire(now);
  this->last_byte_ = now;
  if (byte == 0x1B) {
    if (this->used_ != 0) ++this->errors_;
    this->buffer_[0] = byte;
    this->used_ = 1;
    return false;
  }
  if (this->used_ == 0) return false;
  this->buffer_[this->used_++] = byte;
  if (byte == '&' || this->used_ == FRAME_SIZE) {
    bool valid = this->used_ == FRAME_SIZE && decode_response(this->buffer_, response);
    this->clear();
    if (!valid) ++this->errors_;
    return valid;
  }
  return false;
}

bool is_write(Command command) { return command >= Command::OFF; }

const char *command_name(Command command) {
  static const char *const NAMES[] = {
      "phase", "room", "d3000", "d4000", "flue", "exhaust", "alarm", "inputs", "target",
      "pcb", "firmware", "board", "total_runtime", "service_runtime",
    #ifdef USE_DUEPI_INSTALLER
      "installer_read",
    #else
      "unavailable",
    #endif
      "off", "start_9200", "start_e670", "start_e693", "set_target", "set_level", "reset",
    #ifdef USE_DUEPI_INSTALLER
      "installer_write", "custom",
    #else
      "unavailable", "unavailable",
    #endif
      "start_generic"};
  auto index = static_cast<size_t>(command);
  return index < sizeof(NAMES) / sizeof(NAMES[0]) ? NAMES[index] : "unknown";
}

bool build_request(const Request &request, WireFrame &frame) {
  static const char *const READS[] = {
      "D900", "D100", "D300", "D400", "D000", "EF00", "DA00",
      "C000", "C600", "DF00", "DC00", "C400", "ED00", "EE00"};
  auto ordinal = static_cast<size_t>(request.command);
  if (ordinal < sizeof(READS) / sizeof(READS[0])) return encode_command(READS[ordinal], 4, frame);
  switch (request.command) {
#ifdef USE_DUEPI_INSTALLER
    case Command::INSTALLER_READ: {
      if (request.parameter > 255) return false;
      char payload[] = {'L', HEX[request.parameter >> 4], HEX[request.parameter & 15], '0', '0', '0'};
      return encode_payload(payload, sizeof(payload), frame);
    }
    case Command::INSTALLER_WRITE: {
      if (request.parameter > 255 || request.argument > 255) return false;
      char payload[] = {'S', HEX[request.parameter >> 4], HEX[request.parameter & 15],
                        HEX[request.argument >> 4], HEX[request.argument & 15], '0'};
      return encode_payload(payload, sizeof(payload), frame);
    }
    case Command::CUSTOM: return encode_payload(request.payload.data(), request.payload.size(), frame);
  #endif
    case Command::RESET: return encode_command("D600", 4, frame);
    case Command::OFF: return encode_command("F000", 4, frame);
    case Command::START_GENERIC: return encode_command("F001", 4, frame);
    case Command::START_9200: return encode_command("F005", 4, frame);
    case Command::START_E670:
    case Command::START_E693: return encode_command("F006", 4, frame);
    case Command::SET_TARGET: {
      if (request.argument < 10 || request.argument > 35) return false;
      char body[] = {'F', '2', HEX[request.argument >> 4], HEX[request.argument & 15]};
      return encode_command(body, sizeof(body), frame);
    }
    case Command::SET_LEVEL: {
      if (request.argument < 1 || request.argument > 5) return false;
      char body[] = {'F', '0', '0', HEX[request.argument]};
      return encode_command(body, sizeof(body), frame);
    }
    default: return false;
  }
}

const char *phase_name(uint16_t phase) {
  switch (phase) {
    case 0x0000: return "Off";
    case 0x0101: return "Ignition";
    case 0x0102: return "Ignition: fuel loading";
    case 0x0103: return "Ignition: flame";
    case 0x0104: return "Ignition: ventilation";
    case 0x0105: return "Ignition: E693";
    case 0x010A: return "Delayed ignition";
    case 0x0132: return "Ignition: V100 fuel loading";
    case 0x0133: return "Ignition: V100";
    case 0x0200: return "Heating: cleaning";
    case 0x0201: return "Heating";
    case 0x0301: return "Stopping";
    case 0x0401: return "Cleaning";
    case 0x0801: return "Cooldown 1";
    case 0x0802: return "Cooldown 2";
    case 0x1000: return "Eco idle";
    case 0x1001: return "Eco cooldown 1";
    case 0x1002: return "Eco cooldown 2";
    case 0x1003: return "Eco off";
    default: return "Unknown phase";
  }
}

bool known_phase(uint16_t phase) { return phase_name(phase)[0] != 'U'; }

bool accepts(const Request &request, const Response &response) {
  switch (request.command) {
#ifdef USE_DUEPI_INSTALLER
    case Command::CUSTOM: return true;
#endif
    case Command::ROOM: return response.word() <= 1000;
    case Command::TARGET: return true;
    case Command::D3000:
    case Command::D4000: return response.word() <= 255;
    case Command::FLUE: return response.word() <= 1500;
    case Command::PCB: return response.word() <= 200;
    case Command::EXHAUST: return response.word() <= 10000;
    case Command::FIRMWARE: return response.word() != 0 && response.word() != 0xFFFF;
    case Command::BOARD: return response.byte() <= 1;
    default: return !is_write(request.command) || response.data == 0;
  }
}

}  // namespace duepi
}  // namespace esphome