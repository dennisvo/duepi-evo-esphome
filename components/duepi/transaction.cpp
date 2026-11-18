#include "transaction.h"

namespace esphome {
namespace duepi {

void TransactionEngine::remove_(size_t index) {
  for (size_t next = index + 1; next < this->size_; ++next) this->queue_[next - 1] = this->queue_[next];
  --this->size_;
}

bool TransactionEngine::enqueue(const Request &request, uint32_t now) {
  WireFrame frame{};
  if (!build_request(request, frame) || (is_write(request.command) && this->recovering_)) {
    this->io_.result(request, Outcome::REJECTED, nullptr);
    return false;
  }
  if (request.command == Command::OFF) this->cancel_writes();
  if (this->continuation_pending_ && this->continuation_.request.command == request.command &&
      (request.command == Command::SET_TARGET || request.command == Command::SET_LEVEL)) {
    Request cancelled = this->continuation_.request;
    this->continuation_ = Entry{request, now, 0};
    this->io_.result(cancelled, Outcome::CANCELLED, nullptr);
    this->io_.result(request, Outcome::QUEUED, nullptr);
    return true;
  }
  for (size_t index = 0; index < this->size_; ++index) {
    if (this->queue_[index].request.command != request.command) continue;
    if (request.command == Command::INSTALLER_READ && this->queue_[index].request.parameter != request.parameter) continue;
    if (!is_write(request.command)) return true;
    if (request.command == Command::SET_TARGET || request.command == Command::SET_LEVEL) {
      Request cancelled = this->queue_[index].request;
      this->remove_(index);
      this->io_.result(cancelled, Outcome::CANCELLED, nullptr);
      break;
    }
  }
  if ((!is_write(request.command) && this->size_ >= 4) || this->size_ == this->queue_.size()) {
    this->io_.result(request, Outcome::REJECTED, nullptr);
    return false;
  }
  size_t position = this->size_;
  if (is_write(request.command)) {
    position = 0;
    while (position < this->size_ && is_write(this->queue_[position].request.command)) ++position;
    for (size_t index = this->size_; index > position; --index) this->queue_[index] = this->queue_[index - 1];
  }
  this->queue_[position] = Entry{request, now, 0};
  ++this->size_;
  this->io_.result(request, Outcome::QUEUED, nullptr);
  return true;
}

bool TransactionEngine::off_pending() const {
  if ((this->active_ && this->pending_.request.command == Command::OFF) ||
      (this->continuation_pending_ && this->continuation_.request.command == Command::OFF)) return true;
  for (size_t index = 0; index < this->size_; ++index)
    if (this->queue_[index].request.command == Command::OFF) return true;
  return false;
}

void TransactionEngine::cancel_writes(bool include_parameters) {
  if (this->continuation_pending_ && is_write(this->continuation_.request.command) &&
      (include_parameters || this->continuation_.request.command != Command::INSTALLER_WRITE)) {
    Request cancelled = this->continuation_.request;
    this->continuation_pending_ = false;
    this->io_.result(cancelled, Outcome::CANCELLED, nullptr);
  }
  for (size_t index = 0; index < this->size_;) {
    if (!is_write(this->queue_[index].request.command) ||
      (!include_parameters && this->queue_[index].request.command == Command::INSTALLER_WRITE)) {
      ++index;
      continue;
    }
    Request cancelled = this->queue_[index].request;
    this->remove_(index);
    this->io_.result(cancelled, Outcome::CANCELLED, nullptr);
  }
}

void TransactionEngine::cancel_queued() {
  if (this->continuation_pending_) {
    Request cancelled = this->continuation_.request;
    this->continuation_pending_ = false;
    this->io_.result(cancelled, Outcome::CANCELLED, nullptr);
  }
  while (this->size_ != 0) {
    Request cancelled = this->queue_[0].request;
    this->remove_(0);
    this->io_.result(cancelled, Outcome::CANCELLED, nullptr);
  }
  if (this->retry_pending_) {
    this->retry_pending_ = false;
    this->io_.result(this->retry_.request, Outcome::CANCELLED, nullptr);
  }
}

#ifdef USE_DUEPI_INSTALLER
void TransactionEngine::cancel_installer() {
  if (this->continuation_pending_ && is_installer(this->continuation_.request.command) &&
      !this->continuation_.request.standby) {
    Request cancelled = this->continuation_.request;
    this->continuation_pending_ = false;
    this->io_.result(cancelled, Outcome::CANCELLED, nullptr);
  }
  for (size_t index = 0; index < this->size_;) {
    if (!is_installer(this->queue_[index].request.command) || this->queue_[index].request.standby) {
      ++index;
      continue;
    }
    Request cancelled = this->queue_[index].request;
    this->remove_(index);
    this->io_.result(cancelled, Outcome::CANCELLED, nullptr);
  }
  if (this->retry_pending_ && is_installer(this->retry_.request.command) && !this->retry_.request.standby) {
    this->retry_pending_ = false;
    this->io_.result(this->retry_.request, Outcome::CANCELLED, nullptr);
  }
}
#endif

void TransactionEngine::resynchronize(uint32_t now) {
  this->cancel_queued();
  if (this->active_) {
    this->active_ = false;
    this->io_.result(this->pending_.request,
                     is_write(this->pending_.request.command) ? Outcome::INDETERMINATE : Outcome::CANCELLED,
                     nullptr);
  }
  this->parser_.clear();
  this->recovering_ = true;
  this->last_activity_ = now;
  this->io_.recovery_started();
}

void TransactionEngine::send_(const Entry &entry, uint32_t now, bool initialized) {
  WireFrame frame{};
  if (!build_request(entry.request, frame)) {
    this->io_.result(entry.request, Outcome::REJECTED, nullptr);
    return;
  }
  if (this->init_command && !initialized && entry.request.command != Command::FIRMWARE) {
    this->io_.diagnostic(entry.request, "waiting for initialization firmware query");
    this->continuation_ = entry;
    this->continuation_pending_ = true;
    this->send_(Entry{Request{Command::FIRMWARE}, now, 0}, now, true);
    return;
  }
  if (!this->io_.may_transmit(entry.request, now)) {
    this->io_.diagnostic(entry.request, "send-time authorization rejected");
    this->io_.result(entry.request, Outcome::REJECTED, nullptr);
    return;
  }
  this->pending_ = entry;
  this->active_ = true;
  this->ever_sent_ = true;
  this->sent_at_ = now;
  this->last_activity_ = now;
  this->parser_.clear();
  this->io_.transmit(frame);
  this->io_.result(entry.request, Outcome::SENT, nullptr);
}

void TransactionEngine::timeout_(uint32_t now) {
  this->io_.diagnostic(this->pending_.request, "response timeout; entering quiet recovery");
  this->active_ = false;
  this->recovering_ = true;
  this->last_activity_ = now;
  this->parser_.clear();
  this->io_.recovery_started();
  this->cancel_queued();
  if (!is_write(this->pending_.request.command) && this->pending_.attempts < this->max_read_retries) {
    this->io_.diagnostic(this->pending_.request, "read retry scheduled after quiet recovery");
    this->retry_ = this->pending_;
    ++this->retry_.attempts;
    this->retry_pending_ = true;
  } else {
    this->io_.result(this->pending_.request,
                     is_write(this->pending_.request.command) ? Outcome::INDETERMINATE : Outcome::TIMED_OUT,
                     nullptr);
  }
}

void TransactionEngine::tick(uint32_t now) {
  this->parser_.expire(now);
  if (this->active_) {
    if (uint32_t(now - this->sent_at_) >= this->timeout) this->timeout_(now);
    return;
  }
  if (this->recovering_) {
    if (uint32_t(now - this->last_activity_) < this->quiet_time) return;
    this->recovering_ = false;
    this->io_.diagnostic(this->pending_.request, "UART quiet recovery complete");
  }
  if (this->ever_sent_ && uint32_t(now - this->last_activity_) < this->spacing) return;
  if (this->retry_pending_) {
    this->retry_pending_ = false;
    this->send_(this->retry_, now);
    return;
  }
  if (this->continuation_pending_) {
    this->continuation_pending_ = false;
    if (uint32_t(now - this->continuation_.queued_at) >= this->max_queue_age) {
      this->io_.result(this->continuation_.request, Outcome::CANCELLED, nullptr);
      return;
    }
    this->send_(this->continuation_, now, true);
    return;
  }
  if (this->size_ == 0) return;
  size_t position = 0;
  if (this->queue_[0].request.command == Command::OFF && this->io_.defer_off() &&
      uint32_t(now - this->queue_[0].queued_at) < this->max_queue_age) {
    if (this->size_ == 1) return;
    position = 1;
  }
  Entry next = this->queue_[position];
  this->remove_(position);
  if (uint32_t(now - next.queued_at) >= this->max_queue_age) {
    this->io_.result(next.request, Outcome::CANCELLED, nullptr);
    return;
  }
  this->send_(next, now);
}

void TransactionEngine::receive(uint8_t byte, uint32_t now) {
  if (this->active_ && uint32_t(now - this->sent_at_) >= this->timeout) this->timeout_(now);
  this->last_activity_ = now;
  if (this->recovering_ || !this->active_) {
    this->parser_.clear();
    return;
  }
  Response response{};
  if (!this->parser_.feed(byte, now, response)) return;
  if (!accepts(this->pending_.request, response)) {
    this->io_.diagnostic(this->pending_.request, "framed response outside expected domain; ignored");
    return;
  }
  this->active_ = false;
  this->io_.result(this->pending_.request, Outcome::ACKNOWLEDGED, &response);
}

}  // namespace duepi
}  // namespace esphome