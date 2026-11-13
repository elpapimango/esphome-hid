#include "hid_streamdeck.h"

#include <algorithm>
#include <cstring>

#include "esphome/components/network/util.h"
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
  g_deck->note_set_report(report_id, report_type, bufsize);
  if (report_type == HID_REPORT_TYPE_FEATURE) {
    g_deck->on_feature_set(report_id, buffer, bufsize);
  } else if (report_type == HID_REPORT_TYPE_INVALID && report_id == 0) {
    // older stacks: raw data of the OUT endpoint, report id in front
    g_deck->on_out_data(buffer, bufsize, true);
  } else if (report_type == HID_REPORT_TYPE_OUTPUT) {
    // Seen on the board: data of the OUT endpoint arrives as type OUTPUT with report id 0 and the id still in front.
    g_deck->on_out_data(buffer, bufsize, report_id == 0);
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
  ESP_LOGCONFIG(TAG, "  Key image server port: %u (0 = off)", this->image_port_);
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
  if (this->image_port_ != 0 && this->httpd_ == nullptr && network::is_connected()) this->start_http_();

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
  uint32_t diag = this->set_report_calls_.load() + this->out_rejected_.load() + this->image_chunks_.load();
  if (diag != this->diag_published_) {
    this->diag_published_ = diag;
    uint32_t t = this->last_type_id_.load();
    ESP_LOGD(TAG, "set_report calls=%u (last type=%u id=%u) out_calls=%u last_len=%u head=%08X rejected=%u chunks=%u other=%u",
             (unsigned) this->set_report_calls_.load(), (unsigned) (t >> 8), (unsigned) (t & 0xFF), (unsigned) this->out_calls_.load(),
             (unsigned) this->out_last_len_.load(), (unsigned) this->out_last_head_.load(), (unsigned) this->out_rejected_.load(),
             (unsigned) this->image_chunks_.load(), (unsigned) this->other_set_.load());
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

void HIDStreamDeck::note_set_report(uint8_t report_id, uint8_t report_type, uint16_t length) {
  this->set_report_calls_++;
  this->last_type_id_.store(((uint32_t) report_type << 8) | report_id);
  if (report_type != HID_REPORT_TYPE_FEATURE && report_type != HID_REPORT_TYPE_OUTPUT && report_type != HID_REPORT_TYPE_INVALID)
    this->other_set_++;
  (void) length;
}

void HIDStreamDeck::on_out_data(const uint8_t *data, uint16_t length, bool has_report_id) {
  this->out_calls_++;
  this->out_last_len_.store(length);
  uint32_t head = 0;
  for (uint16_t i = 0; i < 4 && i < length; i++) head |= (uint32_t) data[i] << (8 * i);
  this->out_last_head_.store(head);
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
  uint8_t key = r[2];
  uint16_t len = r[4] | (r[5] << 8);
  uint16_t page = r[6] | (r[7] << 8);
  if (len > 1016) len = 1016;
  if (key < KEY_COUNT && this->image_port_ != 0) {
    auto &buf = this->assembling_[key];
    if (page == 0) buf.clear();
    if (buf.size() + len <= 32 * 1024) buf.insert(buf.end(), r + 8, r + 8 + len);
    if (r[3] == 1) {
      std::lock_guard<std::mutex> lock(this->image_mutex_);
      this->image_[key] = std::move(buf);
      buf.clear();
      this->image_ver_[key]++;
    }
  }
  if (r[3] == 1) this->images_++;  // the last chunk of one image
}

bool HIDStreamDeck::copy_image(uint8_t key, std::vector<uint8_t> &out, uint32_t &version) {
  if (key >= KEY_COUNT) return false;
  std::lock_guard<std::mutex> lock(this->image_mutex_);
  out = this->image_[key];
  version = this->image_ver_[key];
  return !out.empty();
}

uint32_t HIDStreamDeck::image_version(uint8_t key) {
  std::lock_guard<std::mutex> lock(this->image_mutex_);
  return key < KEY_COUNT ? this->image_ver_[key] : 0;
}

static esp_err_t image_handler(httpd_req_t *req) {
  auto *deck = static_cast<HIDStreamDeck *>(req->user_ctx);
  const char *uri = req->uri;  // /key/<n>.jpg
  int n = -1;
  if (sscanf(uri, "/key/%d", &n) != 1 || n < 0 || n >= KEY_COUNT) {
    httpd_resp_send_404(req);
    return ESP_OK;
  }
  std::vector<uint8_t> jpeg;
  uint32_t ver = 0;
  if (!deck->copy_image(n, jpeg, ver)) {
    httpd_resp_set_status(req, "404 Not Found");
    httpd_resp_send(req, "no image for this key yet", HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
  }
  httpd_resp_set_type(req, "image/jpeg");
  httpd_resp_set_hdr(req, "Cache-Control", "no-cache");
  return httpd_resp_send(req, (const char *) jpeg.data(), jpeg.size());
}

static esp_err_t index_handler(httpd_req_t *req) {
  auto *deck = static_cast<HIDStreamDeck *>(req->user_ctx);
  std::string page = "<html><body style='font-family:sans-serif'><h3>Stream Deck key images</h3>";
  char tmp[160];
  for (uint8_t k = 0; k < KEY_COUNT; k++) {
    snprintf(tmp, sizeof tmp, "<div style='display:inline-block;margin:4px;text-align:center'>R%uC%u<br><img width=96 src='/key/%u.jpg'></div>",
             k / 8, k % 8, k);
    page += tmp;
    if (k % 8 == 7) page += "<br>";
  }
  page += "</body></html>";
  httpd_resp_set_type(req, "text/html");
  return httpd_resp_send(req, page.c_str(), page.size());
}

void HIDStreamDeck::start_http_() {
  httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
  cfg.server_port = this->image_port_;
  cfg.ctrl_port = this->image_port_ + 1;
  cfg.uri_match_fn = httpd_uri_match_wildcard;
  if (httpd_start(&this->httpd_, &cfg) != ESP_OK) {
    ESP_LOGE(TAG, "Could not start the image server on port %u", this->image_port_);
    return;
  }
  httpd_uri_t key_uri = {.uri = "/key/*", .method = HTTP_GET, .handler = image_handler, .user_ctx = this};
  httpd_uri_t index_uri = {.uri = "/", .method = HTTP_GET, .handler = index_handler, .user_ctx = this};
  httpd_register_uri_handler(this->httpd_, &key_uri);
  httpd_register_uri_handler(this->httpd_, &index_uri);
  ESP_LOGI(TAG, "Key images at http://%s:%u/key/<n>.jpg", network::get_use_address(), this->image_port_);
}

void HIDStreamDeckButton::dump_config() {
  LOG_BUTTON("", "HID Stream Deck key", this);
  ESP_LOGCONFIG(TAG, "  Key: %u (row %u, column %u), hold %u ms", this->key_, this->key_ / 8, this->key_ % 8,
                (unsigned) this->hold_ms_);
}

}  // namespace hid_streamdeck
}  // namespace esphome
