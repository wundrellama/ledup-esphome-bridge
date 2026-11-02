#pragma once

#include "esphome/components/esp32_ble/ble.h"
#include "esphome/core/application.h"
#include "esphome/core/component.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#include "ledup_advertise_state.h"
#include "ledup_protocol.h"

#include <algorithm>
#include <array>
#include <cstdint>

namespace esphome::ledup_bridge {

class LEDUPBridge : public Component {
 public:
  float get_setup_priority() const override {
    return setup_priority::AFTER_BLUETOOTH;
  }

  void dump_config() override {
    ESP_LOGCONFIG("LEDUP", "LEDUP ctr010f bridge");
  }

  void send(uint8_t remote_group, uint8_t key_code) {
    if (recovering_) {
      ESP_LOGW("LEDUP", "Rejected command while BLE recovery is pending");
      return;
    }

    const ledup::Command command{remote_group, key_code};
    if (state_.phase() == ledup::AdvertisePhase::IDLE &&
        command_queue_.size() == 0) {
      start_command_(command);
      return;
    }

    if (!command_queue_.push(command)) {
      ESP_LOGW("LEDUP", "Command queue full; dropped group=%u key=%u",
               remote_group, key_code);
      return;
    }
    ESP_LOGD("LEDUP", "Queued group=%u key=%u depth=%u", remote_group,
             key_code, static_cast<unsigned>(command_queue_.size()));
  }

  void loop() override {
    if (recovering_) {
      return;
    }

    const uint32_t now = millis();
    if (state_.phase_timed_out(now, PHASE_TIMEOUT_MS)) {
      recover_("BLE GAP completion timeout");
      return;
    }

    if (state_.phase() == ledup::AdvertisePhase::ADVERTISING) {
      if (stop_retry_pending_) {
        if (time_reached_(now, next_stop_retry_at_)) {
          stop_retry_pending_ = false;
          request_stop_(now);
        }
      } else if (state_.should_stop(now, ADVERTISEMENT_DURATION_MS)) {
        request_stop_(now);
      }
    }

    if (state_.phase() == ledup::AdvertisePhase::IDLE) {
      ledup::Command command{};
      if (command_queue_.pop(command)) {
        start_command_(command);
      }
    }
  }

  void gap_event_handler(esp_gap_ble_cb_event_t event,
                         esp_ble_gap_cb_param_t *param) {
    switch (event) {
      case ESP_GAP_BLE_ADV_DATA_SET_COMPLETE_EVT:
        handle_config_complete_(param);
        break;
      case ESP_GAP_BLE_ADV_START_COMPLETE_EVT:
        handle_start_complete_(param);
        break;
      case ESP_GAP_BLE_ADV_STOP_COMPLETE_EVT:
        handle_stop_complete_(param);
        break;
      default:
        break;
    }
  }

 protected:
  static constexpr uint32_t ADVERTISEMENT_DURATION_MS = 500;
  static constexpr uint32_t PHASE_TIMEOUT_MS = 5000;
  static constexpr uint32_t STOP_RETRY_DELAY_MS = 250;
  static constexpr uint8_t MAX_STOP_RETRIES = 3;

  static bool time_reached_(uint32_t now, uint32_t target) {
    return static_cast<int32_t>(now - target) >= 0;
  }

  void start_command_(ledup::Command command) {
    if (esp32_ble::global_ble == nullptr ||
        !esp32_ble::global_ble->is_active()) {
      ESP_LOGE("LEDUP", "BLE stack is not active");
      if (!command_queue_.push(command)) {
        ESP_LOGW("LEDUP", "Command queue full while BLE was inactive");
      }
      return;
    }

    const uint32_t now = millis();
    if (!state_.begin_config(now)) {
      ESP_LOGW("LEDUP", "Cannot start command while transmitter is busy");
      if (!command_queue_.push(command)) {
        ESP_LOGW("LEDUP", "Command queue full while transmitter was busy");
      }
      return;
    }

    const uint8_t current_packet_id = packet_id_;
    const uint8_t current_click_count = click_count_;
    packet_id_ = current_packet_id == 255 ? 1 : current_packet_id + 1;
    click_count_ = static_cast<uint8_t>(current_click_count + 1);

    const auto payload = ledup::build_manufacturer_payload(
        current_packet_id, command.remote_group, command.key_code,
        current_click_count);
    manufacturer_data_[0] = 0xF0;
    manufacturer_data_[1] = 0xFF;
    std::copy(payload.begin(), payload.end(), manufacturer_data_.begin() + 2);

    ESP_LOGD("LEDUP", "group=%u key=%u packet=%u click=%u",
             command.remote_group, command.key_code, current_packet_id,
             current_click_count);
    ESP_LOG_BUFFER_HEX_LEVEL("LEDUP", manufacturer_data_.data(),
                             manufacturer_data_.size(), ESP_LOG_DEBUG);

    advertisement_ = {};
    advertisement_.set_scan_rsp = false;
    advertisement_.include_name = false;
    advertisement_.include_txpower = false;
    advertisement_.flag = 0x02;
    advertisement_.manufacturer_len = manufacturer_data_.size();
    advertisement_.p_manufacturer_data = manufacturer_data_.data();

    parameters_ = {};
    parameters_.adv_int_min = 0x20;
    parameters_.adv_int_max = 0x40;
    parameters_.adv_type = ADV_TYPE_IND;
    parameters_.own_addr_type = BLE_ADDR_TYPE_PUBLIC;
    parameters_.channel_map = ADV_CHNL_ALL;
    parameters_.adv_filter_policy = ADV_FILTER_ALLOW_SCAN_ANY_CON_ANY;

    const esp_err_t error = esp_ble_gap_config_adv_data(&advertisement_);
    if (error != ESP_OK) {
      ESP_LOGE("LEDUP", "Failed to request advertisement configuration: %s",
               esp_err_to_name(error));
      state_.reset(now);
    }
  }

  void request_stop_(uint32_t now) {
    if (!state_.begin_stop(now)) {
      return;
    }

    const esp_err_t error = esp_ble_gap_stop_advertising();
    if (error != ESP_OK) {
      ESP_LOGE("LEDUP", "Failed to request advertisement stop: %s",
               esp_err_to_name(error));
      state_.stop_request_failed(now);
      schedule_stop_retry_(now);
    }
  }

  void schedule_stop_retry_(uint32_t now) {
    ++stop_retry_count_;
    if (stop_retry_count_ > MAX_STOP_RETRIES) {
      recover_("Advertisement stop failed after retries");
      return;
    }
    stop_retry_pending_ = true;
    next_stop_retry_at_ = now + STOP_RETRY_DELAY_MS;
    ESP_LOGW("LEDUP", "Will retry advertisement stop (%u/%u)",
             stop_retry_count_, MAX_STOP_RETRIES);
  }

  void recover_(const char *reason) {
    recovering_ = true;
    ESP_LOGE("LEDUP", "%s; rebooting to reset BLE", reason);
    App.safe_reboot();
  }

  void handle_config_complete_(esp_ble_gap_cb_param_t *param) {
    if (state_.phase() != ledup::AdvertisePhase::CONFIGURING) {
      return;
    }

    const uint32_t now = millis();
    const bool success =
        param != nullptr && param->adv_data_cmpl.status == ESP_BT_STATUS_SUCCESS;
    if (!state_.config_complete(success, now)) {
      ESP_LOGE("LEDUP", "Advertisement configuration failed");
      return;
    }

    const esp_err_t error = esp_ble_gap_start_advertising(&parameters_);
    if (error != ESP_OK) {
      ESP_LOGE("LEDUP", "Failed to request advertisement start: %s",
               esp_err_to_name(error));
      state_.reset(now);
    }
  }

  void handle_start_complete_(esp_ble_gap_cb_param_t *param) {
    if (state_.phase() != ledup::AdvertisePhase::STARTING) {
      return;
    }

    const bool success =
        param != nullptr && param->adv_start_cmpl.status == ESP_BT_STATUS_SUCCESS;
    if (!state_.start_complete(success, millis())) {
      ESP_LOGE("LEDUP", "Advertisement start failed");
      return;
    }

    stop_retry_count_ = 0;
    stop_retry_pending_ = false;
    ESP_LOGD("LEDUP", "Advertisement started");
  }

  void handle_stop_complete_(esp_ble_gap_cb_param_t *param) {
    if (state_.phase() != ledup::AdvertisePhase::STOPPING) {
      return;
    }

    const uint32_t now = millis();
    const bool success =
        param != nullptr && param->adv_stop_cmpl.status == ESP_BT_STATUS_SUCCESS;
    if (!state_.stop_complete(success, now)) {
      ESP_LOGE("LEDUP", "Advertisement stop failed");
      schedule_stop_retry_(now);
      return;
    }

    stop_retry_count_ = 0;
    stop_retry_pending_ = false;
    ESP_LOGD("LEDUP", "Advertisement complete");
  }

  ledup::AdvertiseState state_{};
  ledup::CommandQueue<8> command_queue_{};
  std::array<uint8_t, 18> manufacturer_data_{};
  esp_ble_adv_data_t advertisement_{};
  esp_ble_adv_params_t parameters_{};
  uint32_t next_stop_retry_at_{0};
  uint8_t packet_id_{1};
  uint8_t click_count_{0};
  uint8_t stop_retry_count_{0};
  bool stop_retry_pending_{false};
  bool recovering_{false};
};

}  // namespace esphome::ledup_bridge
