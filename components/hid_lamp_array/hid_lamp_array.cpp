#include "hid_lamp_array.h"
#include "esphome/core/hal.h"
#include "esphome/core/log.h"

#ifdef USE_ESP32

#ifdef HID_LAMP_ARRAY_SUPPORTED

#include "tinyusb.h"
#include "tusb.h"
#include "class/hid/hid_device.h"

namespace esphome {
namespace hid_lamp_array {

static const char *const TAG = "hid_lamp_array";

HIDLampArray *g_hid_lamp_array_instance = nullptr;

// Standalone device: LampArray owns report IDs 1-6.
#define REPORT_ID_LAMP_ARRAY_BASE 1

// A multi-update report plus its report ID has to fit TinyUSB's HID buffer, or
// the host's frames arrive truncated. Add -DCFG_TUD_HID_EP_BUFSIZE=64.
static_assert(CFG_TUD_HID_EP_BUFSIZE >= lamp_array_core::LAMP_ARRAY_MAX_REPORT_SIZE + 1,
              "CFG_TUD_HID_EP_BUFSIZE is too small for LampArray reports; build with -DCFG_TUD_HID_EP_BUFSIZE=64");

static const uint8_t hid_report_descriptor[] = {
    LAMP_ARRAY_HID_REPORT_DESCRIPTOR(REPORT_ID_LAMP_ARRAY_BASE),
};

static const tusb_desc_device_t device_descriptor = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = 0x0200,
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor = 0x303A,
    .idProduct = 0x4006,  // Custom PID for HID LampArray
    .bcdDevice = 0x0100,
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01,
};

static const char *string_descriptors[] = {
    (const char[]){0x09, 0x04},
    "ESPHome",
    "HID LampArray",
    "123456",
};

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)
#define EPNUM_HID 0x81

static const uint8_t configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, CONFIG_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_HID_DESCRIPTOR(0, 0, HID_ITF_PROTOCOL_NONE, sizeof(hid_report_descriptor), EPNUM_HID, CFG_TUD_HID_EP_BUFSIZE,
                       10),
};

extern "C" {
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) { return hid_report_descriptor; }

// LampArray is entirely Feature reports: the host GETs the static geometry and
// SETs the colours, all over the control endpoint.
uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer,
                               uint16_t reqlen) {
  if (report_type != HID_REPORT_TYPE_FEATURE || g_hid_lamp_array_instance == nullptr)
    return 0;
  if (report_id < REPORT_ID_LAMP_ARRAY_BASE ||
      report_id >= REPORT_ID_LAMP_ARRAY_BASE + lamp_array_core::LAMP_ARRAY_REPORT_COUNT)
    return 0;
  return g_hid_lamp_array_instance->core().handle_get_report(report_id - REPORT_ID_LAMP_ARRAY_BASE, buffer, reqlen);
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer,
                           uint16_t bufsize) {
  if (report_type != HID_REPORT_TYPE_FEATURE || g_hid_lamp_array_instance == nullptr)
    return;
  if (report_id < REPORT_ID_LAMP_ARRAY_BASE ||
      report_id >= REPORT_ID_LAMP_ARRAY_BASE + lamp_array_core::LAMP_ARRAY_REPORT_COUNT)
    return;
  g_hid_lamp_array_instance->core().handle_set_report(report_id - REPORT_ID_LAMP_ARRAY_BASE, buffer, bufsize);
}
}

Color lamp_color_to_esphome(const LampColor &color, uint8_t intensity_levels) {
  if (intensity_levels <= 1)
    return color.intensity == 0 ? Color::BLACK : Color(color.red, color.green, color.blue);
  return Color(color.red, color.green, color.blue) * color.intensity;
}

void HIDLampArray::setup() {
  ESP_LOGI(TAG, "Setting up HID LampArray...");

  g_hid_lamp_array_instance = this;

  if (!this->core_.begin()) {
    ESP_LOGE(TAG, "Failed to allocate state for %u lamps", this->core_.lamp_count());
    this->mark_failed();
    return;
  }

  // Sized here rather than lazily in loop(): triggers register during codegen
  // setup, which runs before any component's setup().
  if (this->want_lamp_updates_ && this->incoming_.size() != this->core_.lamp_count()) {
    this->incoming_.assign(this->core_.lamp_count(), LampColor{0, 0, 0, 0});
    this->published_.assign(this->core_.lamp_count(), LampColor{0, 0, 0, 0});
  }

  tinyusb_config_t tusb_cfg = {
      .port = TINYUSB_PORT_FULL_SPEED_0,
      .phy =
          {
              .skip_setup = false,
              .self_powered = false,
              .vbus_monitor_io = -1,
          },
      .task =
          {
              .size = 4096,
              .priority = 5,
              .xCoreID = 0,
          },
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
    return;
  }

  ESP_LOGI(TAG, "HID LampArray initialized with %u lamps", this->core_.lamp_count());
  this->initialized_ = true;
}

void HIDLampArray::loop() {
  if (!this->initialized_)
    return;

  // Losing the host means nobody is driving the lamps any more.
  const bool connected = this->is_connected();
  if (this->was_connected_ && !connected)
    this->core_.on_disconnect();
  this->was_connected_ = connected;

  bool autonomous;
  if (this->core_.poll_autonomous_change(&autonomous)) {
    ESP_LOGD(TAG, "Autonomous mode: %s", ONOFF(autonomous));
    this->autonomous_callbacks_.call(autonomous);
  }

  // Automations run here, never inside the USB callback.
  const uint32_t frame = this->core_.frame();
  if (this->want_lamp_updates_ && frame != this->last_frame_) {
    this->last_frame_ = frame;
    this->core_.snapshot(this->incoming_.data());
    const uint8_t levels = this->core_.intensity_levels();
    for (uint16_t i = 0; i < this->incoming_.size(); i++) {
      const LampColor &color = this->incoming_[i];
      if (std::memcmp(&color, &this->published_[i], sizeof(LampColor)) == 0)
        continue;
      this->published_[i] = color;
      this->lamp_update_callbacks_.call(i, lamp_color_to_esphome(color, levels));
    }
  }
}

void HIDLampArray::dump_config() {
  ESP_LOGCONFIG(TAG, "HID LampArray:");
  ESP_LOGCONFIG(TAG, "  Initialized: %s", YESNO(this->initialized_));
  ESP_LOGCONFIG(TAG, "  Lamps: %u", this->core_.lamp_count());
  ESP_LOGCONFIG(TAG, "  Kind: %u", this->core_.kind());
  ESP_LOGCONFIG(TAG, "  Intensity levels: %u", this->core_.intensity_levels());
  ESP_LOGCONFIG(TAG, "  Report IDs: %d-%d", REPORT_ID_LAMP_ARRAY_BASE,
                REPORT_ID_LAMP_ARRAY_BASE + lamp_array_core::LAMP_ARRAY_REPORT_COUNT - 1);
}

Color HIDLampArray::get_color(uint16_t lamp_id) {
  return lamp_color_to_esphome(this->core_.get_color(lamp_id), this->core_.intensity_levels());
}

bool HIDLampArray::is_connected() {
  if (!this->initialized_)
    return false;
  // tud_mounted() alone is not enough behind a hub: the hub keeps the device
  // enumerated after the PC goes away. tud_suspended() catches the missing SOF.
  return tud_mounted() && !tud_suspended();
}

bool HIDLampArray::is_ready() {
  if (!this->initialized_)
    return false;
  return tud_mounted() && !tud_suspended() && tud_hid_ready();
}

}  // namespace hid_lamp_array
}  // namespace esphome

#else  // HID_LAMP_ARRAY_SUPPORTED

namespace esphome {
namespace hid_lamp_array {

static const char *const TAG = "hid_lamp_array";
HIDLampArray *g_hid_lamp_array_instance = nullptr;

Color lamp_color_to_esphome(const LampColor &color, uint8_t intensity_levels) { return Color::BLACK; }

void HIDLampArray::setup() { ESP_LOGE(TAG, "Only supported on ESP32-S3/S2"); }
void HIDLampArray::loop() {}
void HIDLampArray::dump_config() {}
Color HIDLampArray::get_color(uint16_t lamp_id) { return Color::BLACK; }
bool HIDLampArray::is_connected() { return false; }
bool HIDLampArray::is_ready() { return false; }

}  // namespace hid_lamp_array
}  // namespace esphome

#endif  // HID_LAMP_ARRAY_SUPPORTED
#endif  // USE_ESP32
