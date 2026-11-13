#pragma once

#include <atomic>
#include <mutex>
#include <string>
#include <vector>

#include "esp_http_server.h"

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/components/button/button.h"
#include "esphome/components/sensor/sensor.h"
#include "esphome/core/component.h"

namespace esphome {
namespace hid_streamdeck {

static constexpr uint8_t KEY_COUNT = 32;  // Stream Deck XL: 4 rows x 8 columns, key index = row * 8 + column

/// An ESP32-S3 that enumerates as an Elgato Stream Deck XL (descriptors and feature replies copied from a real one).
class HIDStreamDeck : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::LATE; }

  void set_serial(const std::string &serial) { this->serial_ = serial; }
  void set_ids(uint16_t vendor_id, uint16_t product_id) {
    this->vendor_id_ = vendor_id;
    this->product_id_ = product_id;
  }
  void set_connected_sensor(binary_sensor::BinarySensor *s) { this->connected_sensor_ = s; }
  void set_brightness_sensor(sensor::Sensor *s) { this->brightness_sensor_ = s; }
  void set_images_sensor(sensor::Sensor *s) { this->images_sensor_ = s; }
  /// Serve the last key images as JPEG over HTTP on this port (0 = off).
  void set_image_port(uint16_t port) { this->image_port_ = port; }
  /// The newest JPEG for `key` (empty if none yet); called from the HTTP task.
  bool copy_image(uint8_t key, std::vector<uint8_t> &out, uint32_t &version);
  uint32_t image_version(uint8_t key);

  /// The PC has the deck mounted and is not suspended.
  bool is_connected() const;
  /// Presses key `key` (0 to 31) now, releases it after `hold_ms`.
  void tap(uint8_t key, uint32_t hold_ms);
  /// Sets the state of one key; the report goes out from loop().
  void set_key(uint8_t key, bool pressed);

  // Called from the TinyUSB task.
  void on_out_data(const uint8_t *data, uint16_t length, bool has_report_id);
  void note_set_report(uint8_t report_id, uint8_t report_type, uint16_t length);
  void on_feature_set(uint8_t report_id, const uint8_t *data, uint16_t length);
  uint16_t on_feature_get(uint8_t report_id, uint8_t *buffer, uint16_t length);
  const std::string &serial() const { return this->serial_; }

 protected:
  void handle_image_report_(const uint8_t *report);
  void start_http_();

  uint16_t image_port_{0};
  httpd_handle_t httpd_{nullptr};
  std::mutex image_mutex_;
  std::vector<uint8_t> assembling_[KEY_COUNT];  // only touched by the TinyUSB task
  std::vector<uint8_t> image_[KEY_COUNT];       // finished JPEGs, guarded by image_mutex_
  uint32_t image_ver_[KEY_COUNT]{};

  std::string serial_;
  uint16_t vendor_id_{0x0FD9}, product_id_{0x006C};
  bool initialized_{false};
  bool key_state_[KEY_COUNT]{};
  bool dirty_{true};
  bool connected_published_{false};
  bool connected_known_{false};

  uint8_t out_buf_[1024];
  uint16_t out_len_{0};

  std::atomic<int> brightness_{-1};
  std::atomic<uint32_t> images_{0}, image_chunks_{0}, out_rejected_{0}, features_set_{0};
  // what the TinyUSB callbacks last saw, logged from loop() (they must not log or touch entities themselves)
  std::atomic<uint32_t> set_report_calls_{0}, out_calls_{0}, out_last_len_{0}, out_last_head_{0}, other_set_{0};
  std::atomic<uint32_t> last_type_id_{0};
  uint32_t diag_published_{0};
  int brightness_published_{-2};
  uint32_t images_published_{UINT32_MAX};

  binary_sensor::BinarySensor *connected_sensor_{nullptr};
  sensor::Sensor *brightness_sensor_{nullptr};
  sensor::Sensor *images_sensor_{nullptr};
};

/// One key as a button.
class HIDStreamDeckButton : public button::Button, public Component {
 public:
  void set_parent(HIDStreamDeck *parent) { this->parent_ = parent; }
  void set_key(uint8_t key) { this->key_ = key; }
  void set_hold_ms(uint32_t ms) { this->hold_ms_ = ms; }
  void dump_config() override;

 protected:
  void press_action() override { this->parent_->tap(this->key_, this->hold_ms_); }
  HIDStreamDeck *parent_{nullptr};
  uint8_t key_{0};
  uint32_t hold_ms_{80};
};

}  // namespace hid_streamdeck
}  // namespace esphome
