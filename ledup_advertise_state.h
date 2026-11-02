#pragma once

#include <array>
#include <cstddef>
#include <cstdint>

namespace ledup {

struct Command {
  uint8_t remote_group;
  uint8_t key_code;
};

template<size_t Capacity> class CommandQueue {
 public:
  bool push(Command command) {
    if (count_ == Capacity) {
      return false;
    }
    commands_[tail_] = command;
    tail_ = (tail_ + 1) % Capacity;
    ++count_;
    return true;
  }

  bool pop(Command &command) {
    if (count_ == 0) {
      return false;
    }
    command = commands_[head_];
    head_ = (head_ + 1) % Capacity;
    --count_;
    return true;
  }

  size_t size() const { return count_; }

 private:
  std::array<Command, Capacity> commands_{};
  size_t head_{0};
  size_t tail_{0};
  size_t count_{0};
};

enum class AdvertisePhase : uint8_t {
  IDLE,
  CONFIGURING,
  STARTING,
  ADVERTISING,
  STOPPING,
};

class AdvertiseState {
 public:
  AdvertisePhase phase() const { return phase_; }

  bool begin_config(uint32_t now) {
    if (phase_ != AdvertisePhase::IDLE) {
      return false;
    }
    phase_ = AdvertisePhase::CONFIGURING;
    phase_started_at_ = now;
    return true;
  }

  bool config_complete(bool success, uint32_t now) {
    if (phase_ != AdvertisePhase::CONFIGURING) {
      return false;
    }
    phase_ = success ? AdvertisePhase::STARTING : AdvertisePhase::IDLE;
    phase_started_at_ = now;
    return success;
  }

  bool start_complete(bool success, uint32_t now) {
    if (phase_ != AdvertisePhase::STARTING) {
      return false;
    }
    if (!success) {
      phase_ = AdvertisePhase::IDLE;
      phase_started_at_ = now;
      return false;
    }
    started_at_ = now;
    phase_started_at_ = now;
    phase_ = AdvertisePhase::ADVERTISING;
    return true;
  }

  bool should_stop(uint32_t now, uint32_t duration) const {
    return phase_ == AdvertisePhase::ADVERTISING &&
           static_cast<uint32_t>(now - started_at_) >= duration;
  }

  bool begin_stop(uint32_t now) {
    if (phase_ != AdvertisePhase::ADVERTISING) {
      return false;
    }
    phase_ = AdvertisePhase::STOPPING;
    phase_started_at_ = now;
    return true;
  }

  bool stop_complete(bool success, uint32_t now) {
    if (phase_ != AdvertisePhase::STOPPING) {
      return false;
    }
    phase_ = success ? AdvertisePhase::IDLE : AdvertisePhase::ADVERTISING;
    phase_started_at_ = now;
    return success;
  }

  bool stop_request_failed(uint32_t now) {
    if (phase_ != AdvertisePhase::STOPPING) {
      return false;
    }
    phase_ = AdvertisePhase::ADVERTISING;
    phase_started_at_ = now;
    return true;
  }

  bool phase_timed_out(uint32_t now, uint32_t timeout) const {
    if (phase_ == AdvertisePhase::IDLE ||
        phase_ == AdvertisePhase::ADVERTISING) {
      return false;
    }
    return static_cast<uint32_t>(now - phase_started_at_) >= timeout;
  }

  void reset(uint32_t now = 0) {
    phase_ = AdvertisePhase::IDLE;
    phase_started_at_ = now;
  }

 private:
  AdvertisePhase phase_{AdvertisePhase::IDLE};
  uint32_t started_at_{0};
  uint32_t phase_started_at_{0};
};

}  // namespace ledup
