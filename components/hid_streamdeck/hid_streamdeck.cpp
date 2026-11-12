#include "hid_streamdeck.h"

#include <algorithm>
#include <cstring>

#include "esphome/core/log.h"
#include "esp_mac.h"
#include "tinyusb.h"
#include "class/hid/hid_device.h"

#include "xl_data.h"

// A 1024-byte output report (key images) has to fit one transfer, and so does the 512-byte input report: set
// -DCFG_TUD_HID_EP_BUFSIZE=1024 in the build flags (see examples/streamdeck.yaml).
static_assert(CFG_TUD_HID_EP_BUFSIZE >= 1024, "add -DCFG_TUD_HID_EP_BUFSIZE=1024 to platformio_options: build_flags (see examples/streamdeck.yaml)");

namespace esphome {
namespace hid_streamdeck {

static const char *const TAG = "hid_streamdeck";

static HIDStreamDeck *g_deck = nullptr;

// ---- USB descriptors -------------------------------------------------------------------------------------------------

static tusb_desc_device_t device_descriptor = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0x0FD9,
    .idProduct = 0x006C,
    .bcdDevice = 0x0000,  // as the real XL
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01,
};

static char serial_string[16] = "ESPDECK00000";

static const char *string_descriptors[] = {
    (const char[]){0x09, 0x04},
    "Elgato",
    "Stream Deck XL",
    serial_string,
};

#define EPNUM_OUT 0x02
#define EPNUM_IN 0x81
#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_INOUT_DESC_LEN)

// The real XL: one HID interface with an interrupt IN endpoint (512 bytes) and an OUT endpoint (1024 bytes) at 1 ms.
// This chip only does Full-Speed, where an endpoint carries 64 bytes; the 512-byte input report and the 1024-byte output report
// are split into 64-byte packets by the host and the stack.
static const uint8_t configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, CONFIG_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 500),
    TUD_HID_INOUT_DESCRIPTOR(0, 0, HID_ITF_PROTOCOL_NONE, sizeof(XL_REPORT_DESCRIPTOR), EPNUM_OUT, EPNUM_IN, 64, 1),
};

extern "C" {

uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) { return XL_REPORT_DESCRIPTOR; }

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t reqlen) {
  if (g_deck == nullptr) return 0;
  if (report_type == HID_REPORT_TYPE_FEATURE) return g_deck->on_feature_get(report_id, buffer, reqlen);
  return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer,
                           uint16_t bufsize) {
  if (g_deck == nullptr) return;
  if (report_type == HID_REPORT_TYPE_FEATURE) {
    g_deck->on_feature_set(report_id, buffer, bufsize);
  } else if (report_type == HID_REPORT_TYPE_INVALID && report_id == 0) {
    // data that arrived on the OUT endpoint: the stack does not strip the report id there
    g_deck->on_out_data(buffer, bufsize, true);
  } else if (report_type == HID_REPORT_TYPE_OUTPUT) {
    g_deck->on_out_data(buffer, bufsize, false);
  }
}

}  // extern "C"

// ---- the component -----------------------------------------------------------------------------------------------------

void HIDStreamDeck::setup() {
  g_deck = this;
  if (this->serial_.empty()) {
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char text[16];
    snprintf(text, sizeof text, "ESPD%02X%02X%02X%02X", mac[2], mac[3], mac[4], mac[5]);
    this->serial_ = text;
  }
  snprintf(serial_string, sizeof serial_string, "%.12s", this->serial_.c_str());
  device_descriptor.idVendor = this->vendor_id_;
  device_descriptor.idProduct = this->product_id_;

  tinyusb_config_t tusb_cfg = {
      .port = TINYUSB_PORT_FULL_SPEED_0,
      .phy = {.skip_setup = false, .self_powered = false, .vbus_monitor_io = -1},
      .task = {.size = 4096, .priority = 5, .xCoreID = 0},
      .descriptor =
          {
              .device = &device_descriptor,
              .qualifier = nullptr,
              .string = string_descriptors,
              .string_count = sizeof(string_descriptors) / sizeof(string_descriptors[0]),
              .full_speed_config = configuration_descriptor,
              .high_speed_config = nullptr,
          },
      .event_cb = nullptr,
      .event_arg = nullptr,
  };
  esp_err_t ret = tinyusb_driver_install(&tusb_cfg);
  if (ret != ESP_OK) {
    ESP_LOGE(TAG, "TinyUSB driver install failed: %s", esp_err_to_name(ret));
    this->mark_failed();
    return;
  }
  this->initialized_ = true;
  ESP_LOGI(TAG, "Stream Deck XL %04X:%04X, serial %s", this->vendor_id_, this->product_id_, serial_string);
}

void HIDStreamDeck::dump_config() {
  ESP_LOGCONFIG(TAG, "HID Stream Deck (XL, %u keys):", KEY_COUNT);
  ESP_LOGCONFIG(TAG, "  USB id: %04X:%04X", this->vendor_id_, this->product_id_);
  ESP_LOGCONFIG(TAG, "  Serial: %s", this->serial_.c_str());
}

bool HIDStreamDeck::is_connected() const { return this->initialized_ && tud_mounted() && !tud_suspended(); }

void HIDStreamDeck::set_key(uint8_t key, bool pressed) {
  if (key >= KEY_COUNT) return;
  this->key_state_[key] = pressed;
  this->dirty_ = true;
}

void HIDStreamDeck::tap(uint8_t key, uint32_t hold_ms) {
  if (key >= KEY_COUNT) return;
  if (!this->is_connected()) ESP_LOGW(TAG, "key %u pressed while the PC has no connection to the deck", key);
  this->set_key(key, true);
  this->set_timeout("release-" + std::to_string(key), hold_ms, [this, key]() { this->set_key(key, false); });
}

void HIDStreamDeck::loop() {
  if (!this->initialized_) return;

  // key states: the report as the real XL sends it (report id 1, 511 bytes: 00, key count, 00, one byte per key)
  if (this->dirty_ && tud_mounted() && tud_hid_ready()) {
    uint8_t payload[511] = {0};
    payload[1] = KEY_COUNT;
    for (uint8_t i = 0; i < KEY_COUNT; i++) payload[3 + i] = this->key_state_[i] ? 1 : 0;
    if (tud_hid_report(1, payload, sizeof payload)) this->dirty_ = false;
  }

  bool connected = this->is_connected();
  if (!this->connected_known_ || connected != this->connected_published_) {
    this->connected_known_ = true;
    this->connected_published_ = connected;
    if (this->connected_sensor_ != nullptr) this->connected_sensor_->publish_state(connected);
    ESP_LOGI(TAG, "host %s", connected ? "connected" : "not connected");
  }
  int brightness = this->brightness_.load();
  if (brightness != this->brightness_published_) {
    this->brightness_published_ = brightness;
    if (this->brightness_sensor_ != nullptr && brightness >= 0) this->brightness_sensor_->publish_state(brightness);
  }
  uint32_t images = this->images_.load();
  if (images != this->images_published_) {
    this->images_published_ = images;
    if (this->images_sensor_ != nullptr) this->images_sensor_->publish_state(images);
  }
}

uint16_t HIDStreamDeck::on_feature_get(uint8_t report_id, uint8_t *buffer, uint16_t length) {
  // 31 payload bytes (the stack puts the report id in front)
  uint8_t reply[31] = {0};
  if (report_id == 0x06) {  // serial number: 0x0C, then the string
    reply[0] = 12;
    memcpy(reply + 1, this->serial_.c_str(), std::min<size_t>(12, this->serial_.size()));
  } else {
    bool known = false;
    for (const auto &r : XL_FEATURE_REPLIES) {
      if (r.id == report_id) {
        memcpy(reply, r.payload, sizeof reply);
        known = true;
        break;
      }
    }
    if (!known) return 0;
  }
  uint16_t n = std::min<uint16_t>(length, sizeof reply);
  memcpy(buffer, reply, n);
  return n;
}

void HIDStreamDeck::on_feature_set(uint8_t report_id, const uint8_t *data, uint16_t length) {
  this->features_set_++;
  if (report_id != 0x03 || length < 1) return;
  switch (data[0]) {
    case 0x08:  // brightness, percent
      if (length >= 2) this->brightness_.store(data[1] > 100 ? 100 : data[1]);
      break;
    case 0x02:  // reset
      this->brightness_.store(-1);
      break;
    default:
      break;  // 0x05 and 0x0D at start-up: meaning unknown, accepted
  }
}

void HIDStreamDeck::on_out_data(const uint8_t *data, uint16_t length, bool has_report_id) {
  // Output report 2 is 1024 bytes: [02][07 key last len_lo len_hi page_lo page_hi][payload ...], and may arrive in pieces.
  uint16_t at = 0;
  if (!has_report_id && this->out_len_ == 0) {
    this->out_buf_[0] = 0x02;  // the stack took the report id off; put it back so that the layout is one
    this->out_len_ = 1;
  }
  while (at < length) {
    if (this->out_len_ == 0 && data[at] != 0x02) {  // not the start of an output report
      this->out_rejected_++;
      return;
    }
    uint16_t take = std::min<uint16_t>(length - at, sizeof this->out_buf_ - this->out_len_);
    memcpy(this->out_buf_ + this->out_len_, data + at, take);
    this->out_len_ += take;
    at += take;
    if (this->out_len_ >= sizeof this->out_buf_) {
      this->handle_image_report_(this->out_buf_);
      this->out_len_ = 0;
    }
  }
}

void HIDStreamDeck::handle_image_report_(const uint8_t *r) {
  if (r[1] != 0x07) return;  // "set key image" is the only output command seen
  this->image_chunks_++;
  if (r[3] == 1) this->images_++;  // the last chunk of one image
}

void HIDStreamDeckButton::dump_config() {
  LOG_BUTTON("", "HID Stream Deck key", this);
  ESP_LOGCONFIG(TAG, "  Key: %u (row %u, column %u), hold %u ms", this->key_, this->key_ / 8, this->key_ % 8,
                (unsigned) this->hold_ms_);
}

}  // namespace hid_streamdeck
}  // namespace esphome
