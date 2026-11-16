#pragma once

#include "protocol.h"

namespace esphome {
namespace duepi {

enum class Outcome : uint8_t { QUEUED, SENT, ACKNOWLEDGED, REJECTED, TIMED_OUT, INDETERMINATE, CANCELLED, VERIFIED };

class TransactionIO {
 public:
  virtual ~TransactionIO() = default;
  virtual void transmit(const WireFrame &frame) = 0;
  virtual void result(const Request &request, Outcome outcome, const Response *response) = 0;
  virtual bool may_transmit(const Request &, uint32_t) { return true; }
  virtual bool defer_off() const { return false; }
  virtual void recovery_started() {}
  virtual void diagnostic(const Request &, const char *) {}
};

class TransactionEngine {
 public:
  explicit TransactionEngine(TransactionIO &io) : io_(io) {}
  bool enqueue(const Request &request, uint32_t now);
  void tick(uint32_t now);
  void receive(uint8_t byte, uint32_t now);
  void cancel_queued();
  void cancel_writes();
  bool off_pending() const;
#ifdef USE_DUEPI_INSTALLER
  void cancel_installer();
#endif
  void resynchronize(uint32_t now);
  bool idle() const { return !this->active_ && !this->recovering_ && !this->retry_pending_ && !this->continuation_pending_; }
  bool ready_to_poll() const {
    return this->idle() && (this->size_ == 0 ||
        (this->size_ == 1 && this->queue_[0].request.command == Command::OFF && this->io_.defer_off()));
  }
  bool recovering() const { return this->recovering_; }
  size_t queued() const { return this->size_; }
  uint32_t parser_errors() const { return this->parser_.errors(); }
  uint32_t timeout{800};
  uint32_t quiet_time{250};
  uint32_t spacing{100};
  uint32_t max_queue_age{5000};
  uint8_t max_read_retries{1};
  bool init_command{false};

 protected:
  struct Entry { Request request{}; uint32_t queued_at{0}; uint8_t attempts{0}; };
  void remove_(size_t index);
  void send_(const Entry &entry, uint32_t now, bool initialized = false);
  void timeout_(uint32_t now);
  TransactionIO &io_;
  FrameParser parser_;
  std::array<Entry, 8> queue_{};
  size_t size_{0};
  Entry pending_{};
  Entry retry_{};
  Entry continuation_{};
  bool continuation_pending_{false};
  bool active_{false};
  bool recovering_{false};
  bool retry_pending_{false};
  bool ever_sent_{false};
  uint32_t sent_at_{0};
  uint32_t last_activity_{0};
};

}  // namespace duepi
}  // namespace esphome