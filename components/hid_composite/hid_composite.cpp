#include "hid_composite.h"
#include "esphome/core/log.h"
#include "esphome/core/hal.h"
#include <cinttypes>

#ifdef USE_ESP32

#ifdef HID_COMPOSITE_SUPPORTED

#include "tinyusb.h"
#include "tusb.h"
#include "class/hid/hid_device.h"

namespace esphome {
namespace hid_composite {

static const char *const TAG = "hid_composite";

// Global instance for TinyUSB callback
static HIDComposite *g_hid_composite_instance = nullptr;

// How long buttons/keys stay pressed before the scheduled release.
static const uint32_t MUTE_PULSE_MS = 50;
static const uint32_t CONSUMER_PULSE_MS = 50;
static const uint32_t KEY_PULSE_MS = 10;
static const uint32_t CLICK_HOLD_MS = 10;

// Report IDs
#define REPORT_ID_KEYBOARD   1
#define REPORT_ID_MOUSE      2
#define REPORT_ID_CONSUMER   5   // Consumer Control (media keys, mute)
// Telephony Report IDs - Simplified for Teams compatibility
#define REPORT_ID_TELEPHONY_INPUT  0x03  // Input report (buttons to host)
#define REPORT_ID_TELEPHONY_LED    0x04  // Output report (LEDs from host)

#ifdef USE_HID_COMPOSITE_LAMP_ARRAY
// LampArray claims the next six IDs (6-11), after the five above.
#define REPORT_ID_LAMP_ARRAY_BASE  6

// Throttle for the raw lamp-report dump; see tud_hid_set_report_cb().
static const uint32_t LAMP_LOG_INTERVAL_MS = 1000;

// A multi-update report plus its report ID has to fit TinyUSB's HID buffer, or
// the host's frames arrive truncated. Add -DCFG_TUD_HID_EP_BUFSIZE=64.
static_assert(CFG_TUD_HID_EP_BUFSIZE >= lamp_array_core::LAMP_ARRAY_MAX_REPORT_SIZE + 1,
              "CFG_TUD_HID_EP_BUFSIZE is too small for LampArray reports; build with -DCFG_TUD_HID_EP_BUFSIZE=64");
#endif

// Key codes
enum KeyCode : uint8_t {
  KEY_NONE = 0x00,
  KEY_A = 0x04, KEY_B = 0x05, KEY_C = 0x06, KEY_D = 0x07, KEY_E = 0x08, KEY_F = 0x09,
  KEY_G = 0x0A, KEY_H = 0x0B, KEY_I = 0x0C, KEY_J = 0x0D, KEY_K = 0x0E, KEY_L = 0x0F,
  KEY_M = 0x10, KEY_N = 0x11, KEY_O = 0x12, KEY_P = 0x13, KEY_Q = 0x14, KEY_R = 0x15,
  KEY_S = 0x16, KEY_T = 0x17, KEY_U = 0x18, KEY_V = 0x19, KEY_W = 0x1A, KEY_X = 0x1B,
  KEY_Y = 0x1C, KEY_Z = 0x1D,
  KEY_1 = 0x1E, KEY_2 = 0x1F, KEY_3 = 0x20, KEY_4 = 0x21, KEY_5 = 0x22,
  KEY_6 = 0x23, KEY_7 = 0x24, KEY_8 = 0x25, KEY_9 = 0x26, KEY_0 = 0x27,
  KEY_ENTER = 0x28, KEY_ESC = 0x29, KEY_BACKSPACE = 0x2A, KEY_TAB = 0x2B, KEY_SPACE = 0x2C,
  KEY_MINUS = 0x2D, KEY_EQUAL = 0x2E, KEY_LEFT_BRACE = 0x2F, KEY_RIGHT_BRACE = 0x30,
  KEY_BACKSLASH = 0x31, KEY_SEMICOLON = 0x33, KEY_APOSTROPHE = 0x34, KEY_GRAVE = 0x35,
  KEY_COMMA = 0x36, KEY_PERIOD = 0x37, KEY_SLASH = 0x38, KEY_CAPS_LOCK = 0x39,
  KEY_F1 = 0x3A, KEY_F2 = 0x3B, KEY_F3 = 0x3C, KEY_F4 = 0x3D, KEY_F5 = 0x3E, KEY_F6 = 0x3F,
  KEY_F7 = 0x40, KEY_F8 = 0x41, KEY_F9 = 0x42, KEY_F10 = 0x43, KEY_F11 = 0x44, KEY_F12 = 0x45,
  KEY_PRINT_SCREEN = 0x46, KEY_SCROLL_LOCK = 0x47, KEY_PAUSE = 0x48,
  KEY_INSERT = 0x49, KEY_HOME = 0x4A, KEY_PAGE_UP = 0x4B,
  KEY_DELETE = 0x4C, KEY_END = 0x4D, KEY_PAGE_DOWN = 0x4E,
  KEY_RIGHT_ARROW = 0x4F, KEY_LEFT_ARROW = 0x50, KEY_DOWN_ARROW = 0x51, KEY_UP_ARROW = 0x52,
  // F13-F24 have no keycaps on normal keyboards, which is exactly why F13-F15
  // are the usual keep-awake keys: the host sees activity, nothing visible happens.
  KEY_F13 = 0x68, KEY_F14 = 0x69, KEY_F15 = 0x6A, KEY_F16 = 0x6B, KEY_F17 = 0x6C, KEY_F18 = 0x6D,
  KEY_F19 = 0x6E, KEY_F20 = 0x6F, KEY_F21 = 0x70, KEY_F22 = 0x71, KEY_F23 = 0x72, KEY_F24 = 0x73,
};

// Composite HID Report Descriptor (Keyboard + Mouse)
static const uint8_t hid_report_descriptor[] = {
    // Keyboard
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x06,        // Usage (Keyboard)
    0xA1, 0x01,        // Collection (Application)
    0x85, REPORT_ID_KEYBOARD, // Report ID
    0x05, 0x07,        //   Usage Page (Keyboard)
    0x19, 0xE0,        //   Usage Minimum (Left Control)
    0x29, 0xE7,        //   Usage Maximum (Right GUI)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x08,        //   Report Count (8)
    0x81, 0x02,        //   Input (Data, Variable, Absolute)
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x08,        //   Report Size (8)
    0x81, 0x01,        //   Input (Constant)
    0x95, 0x05,        //   Report Count (5)
    0x75, 0x01,        //   Report Size (1)
    0x05, 0x08,        //   Usage Page (LEDs)
    0x19, 0x01,        //   Usage Minimum (Num Lock)
    0x29, 0x05,        //   Usage Maximum (Kana)
    0x91, 0x02,        //   Output (Data, Variable, Absolute)
    0x95, 0x01,        //   Report Count (1)
    0x75, 0x03,        //   Report Size (3)
    0x91, 0x01,        //   Output (Constant)
    0x95, 0x06,        //   Report Count (6)
    0x75, 0x08,        //   Report Size (8)
    0x15, 0x00,        //   Logical Minimum (0)
    0x26, 0xFF, 0x00,  //   Logical Maximum (255) - must cover F13-F24 (0x68-0x73)
    0x05, 0x07,        //   Usage Page (Keyboard)
    0x19, 0x00,        //   Usage Minimum (0)
    0x2A, 0xFF, 0x00,  //   Usage Maximum (0xFF) - must cover F13-F24 (0x68-0x73)
    0x81, 0x00,        //   Input (Data, Array)
    0xC0,              // End Collection

    // Mouse
    0x05, 0x01,        // Usage Page (Generic Desktop)
    0x09, 0x02,        // Usage (Mouse)
    0xA1, 0x01,        // Collection (Application)
    0x09, 0x01,        //   Usage (Pointer)
    0xA1, 0x00,        //   Collection (Physical)
    0x85, REPORT_ID_MOUSE, // Report ID
    0x05, 0x09,        //     Usage Page (Buttons)
    0x19, 0x01,        //     Usage Minimum (Button 1)
    0x29, 0x03,        //     Usage Maximum (Button 3)
    0x15, 0x00,        //     Logical Minimum (0)
    0x25, 0x01,        //     Logical Maximum (1)
    0x95, 0x03,        //     Report Count (3)
    0x75, 0x01,        //     Report Size (1)
    0x81, 0x02,        //     Input (Data, Variable, Absolute)
    0x95, 0x01,        //     Report Count (1)
    0x75, 0x05,        //     Report Size (5)
    0x81, 0x01,        //     Input (Constant)
    0x05, 0x01,        //     Usage Page (Generic Desktop)
    0x09, 0x30,        //     Usage (X)
    0x09, 0x31,        //     Usage (Y)
    0x09, 0x38,        //     Usage (Wheel)
    0x15, 0x81,        //     Logical Minimum (-127)
    0x25, 0x7F,        //     Logical Maximum (127)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x03,        //     Report Count (3)
    0x81, 0x06,        //     Input (Data, Variable, Relative)
    0x05, 0x0C,        //     Usage Page (Consumer)
    0x0A, 0x38, 0x02,  //     Usage (AC Pan)
    0x15, 0x81,        //     Logical Minimum (-127)
    0x25, 0x7F,        //     Logical Maximum (127)
    0x75, 0x08,        //     Report Size (8)
    0x95, 0x01,        //     Report Count (1)
    0x81, 0x06,        //     Input (Data, Variable, Relative)
    0xC0,              //   End Collection
    0xC0,              // End Collection

    // ============================================
    // Consumer Control - Media Keys & Mute
    // This works with Teams on Windows!
    // ============================================
    0x05, 0x0C,        // Usage Page (Consumer)
    0x09, 0x01,        // Usage (Consumer Control)
    0xA1, 0x01,        // Collection (Application)
    0x85, REPORT_ID_CONSUMER, // Report ID (5)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    0x95, 0x01,        //   Report Count (1)
    
    // Mute - Usage 0xE2
    0x09, 0xE2,        //   Usage (Mute)
    0x81, 0x06,        //   Input (Data, Variable, Relative)
    
    // Volume Up - Usage 0xE9
    0x09, 0xE9,        //   Usage (Volume Increment)
    0x81, 0x06,        //   Input (Data, Variable, Relative)
    
    // Volume Down - Usage 0xEA
    0x09, 0xEA,        //   Usage (Volume Decrement)
    0x81, 0x06,        //   Input (Data, Variable, Relative)
    
    // Padding to byte
    0x95, 0x05,        //   Report Count (5)
    0x81, 0x03,        //   Input (Constant)
    
    0xC0,              // End Collection

    // ============================================
    // Telephony Headset - Microsoft Teams Compatible
    // Simplified descriptor that Teams recognizes
    // ============================================
    0x05, 0x0B,        // Usage Page (Telephony Devices)
    0x09, 0x05,        // Usage (Headset)
    0xA1, 0x01,        // Collection (Application)
    
    // === INPUT REPORT (device -> host) ===
    0x85, 0x03,        // Report ID (3) - Simple ID that Teams expects
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    
    // Hook Switch - Answer/End call
    0x95, 0x01,        //   Report Count (1)
    0x09, 0x20,        //   Usage (Hook Switch)
    0x81, 0x02,        //   Input (Data, Variable, Absolute)
    
    // Phone Mute - Toggle microphone
    0x95, 0x01,        //   Report Count (1)
    0x09, 0x2F,        //   Usage (Phone Mute)
    0x81, 0x06,        //   Input (Data, Variable, Relative) - RELATIVE is key!
    
    // Padding to byte boundary
    0x95, 0x06,        //   Report Count (6)
    0x81, 0x03,        //   Input (Constant)
    
    // === OUTPUT REPORT (host -> device) ===
    0x85, 0x04,        // Report ID (4) - LED report
    0x05, 0x08,        //   Usage Page (LEDs)
    0x15, 0x00,        //   Logical Minimum (0)
    0x25, 0x01,        //   Logical Maximum (1)
    0x75, 0x01,        //   Report Size (1)
    
    // Mute LED - Teams sends mute state here
    0x95, 0x01,        //   Report Count (1)
    0x09, 0x09,        //   Usage (Mute)
    0x91, 0x02,        //   Output (Data, Variable, Absolute)
    
    // Off-Hook LED - In-call indicator
    0x95, 0x01,        //   Report Count (1)
    0x09, 0x17,        //   Usage (Off-Hook)
    0x91, 0x02,        //   Output (Data, Variable, Absolute)
    
    // Ring LED
    0x95, 0x01,        //   Report Count (1)
    0x09, 0x18,        //   Usage (Ring)
    0x91, 0x02,        //   Output (Data, Variable, Absolute)
    
    // Hold LED
    0x95, 0x01,        //   Report Count (1)
    0x09, 0x20,        //   Usage (Hold)
    0x91, 0x02,        //   Output (Data, Variable, Absolute)
    
    // Microphone LED (some apps use this for mute)
    0x95, 0x01,        //   Report Count (1)
    0x09, 0x21,        //   Usage (Microphone)
    0x91, 0x02,        //   Output (Data, Variable, Absolute)
    
    // Padding
    0x95, 0x03,        //   Report Count (3)
    0x91, 0x03,        //   Output (Constant)

    0xC0,              // End Collection

#ifdef USE_HID_COMPOSITE_LAMP_ARRAY
    // ============================================
    // LampArray - Windows Dynamic Lighting
    // Report IDs 6-11, all Feature reports
    // ============================================
    LAMP_ARRAY_HID_REPORT_DESCRIPTOR(REPORT_ID_LAMP_ARRAY_BASE),
#endif
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
    // Windows caches HID descriptors per VID/PID, so a build with LampArray
    // enumerates under its own PID rather than reusing a cached descriptor.
#ifdef USE_HID_COMPOSITE_LAMP_ARRAY
    .idProduct = 0x4007,  // Composite + LampArray
#else
    .idProduct = 0x4004,  // Different from mouse-only and keyboard-only
#endif
    .bcdDevice = 0x0101,  // bumped when the report descriptor changed, so hosts re-read it
    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,
    .bNumConfigurations = 0x01,
};

static const char *string_descriptors[] = {
    (const char[]){0x09, 0x04},
    "ESPHome",
    "HID Composite",
    "123456",
};

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + TUD_HID_DESC_LEN)
#define EPNUM_HID 0x81

static const uint8_t configuration_descriptor[] = {
    TUD_CONFIG_DESCRIPTOR(1, 1, 0, CONFIG_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),
    TUD_HID_DESCRIPTOR(0, 0, HID_ITF_PROTOCOL_NONE, sizeof(hid_report_descriptor), EPNUM_HID, CFG_TUD_HID_EP_BUFSIZE, 10),
};

extern "C" {
uint8_t const *tud_hid_descriptor_report_cb(uint8_t instance) { return hid_report_descriptor; }

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t *buffer, uint16_t reqlen) {
#ifdef USE_HID_COMPOSITE_LAMP_ARRAY
  // LampArray is the only feature-report consumer: the host GETs the static
  // lamp geometry from us.
  if (report_type == HID_REPORT_TYPE_FEATURE && g_hid_composite_instance != nullptr &&
      report_id >= REPORT_ID_LAMP_ARRAY_BASE &&
      report_id < REPORT_ID_LAMP_ARRAY_BASE + lamp_array_core::LAMP_ARRAY_REPORT_COUNT) {
    return g_hid_composite_instance->core().handle_get_report(report_id - REPORT_ID_LAMP_ARRAY_BASE, buffer, reqlen);
  }
#endif
  return 0;
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type, uint8_t const *buffer, uint16_t bufsize) {
  // Verbose raw dump of every report from the host, for protocol debugging.
  bool log_this_report = true;

#ifdef USE_HID_COMPOSITE_LAMP_ARRAY
  // Lamp updates arrive up to ~30x/second. Logging every one would swamp the
  // UART and stall this task, so they are throttled to one line per second with
  // a count of what was skipped. Everything else logs unthrottled as before.
  if (report_id >= REPORT_ID_LAMP_ARRAY_BASE &&
      report_id < REPORT_ID_LAMP_ARRAY_BASE + lamp_array_core::LAMP_ARRAY_REPORT_COUNT) {
    static uint32_t last_lamp_log = 0;
    static uint32_t lamp_reports_skipped = 0;
    const uint32_t now = millis();
    if (now - last_lamp_log < LAMP_LOG_INTERVAL_MS) {
      lamp_reports_skipped++;
      log_this_report = false;
    } else {
      last_lamp_log = now;
      if (lamp_reports_skipped > 0) {
        ESP_LOGV("HID_RAW", ">>> ... %" PRIu32 " further lamp reports not logged", lamp_reports_skipped);
        lamp_reports_skipped = 0;
      }
    }
  }
#endif

  const char* type_str = (report_type == HID_REPORT_TYPE_OUTPUT) ? "OUTPUT" :
                         (report_type == HID_REPORT_TYPE_FEATURE) ? "FEATURE" : "UNKNOWN";

  if (log_this_report) {
    // Build hex string of all bytes
    char hex_buf[64] = {0};
    for (uint16_t i = 0; i < bufsize && i < 20; i++) {
      snprintf(hex_buf + i*3, 4, "%02X ", buffer[i]);
    }
    ESP_LOGV("HID_RAW", ">>> HOST REPORT: instance=%d, id=0x%02X, type=%s, size=%d, data=[%s]",
             instance, report_id, type_str, bufsize, hex_buf);
  }

  if (g_hid_composite_instance == nullptr)
    return;

  if (report_type == HID_REPORT_TYPE_OUTPUT) {
    // Telephony LED states
    g_hid_composite_instance->process_host_report(report_id, buffer, bufsize);
    return;
  }

#ifdef USE_HID_COMPOSITE_LAMP_ARRAY
  // Feature reports: the host pushing lamp colours and control state.
  if (report_type == HID_REPORT_TYPE_FEATURE && report_id >= REPORT_ID_LAMP_ARRAY_BASE &&
      report_id < REPORT_ID_LAMP_ARRAY_BASE + lamp_array_core::LAMP_ARRAY_REPORT_COUNT) {
    g_hid_composite_instance->core().handle_set_report(report_id - REPORT_ID_LAMP_ARRAY_BASE, buffer, bufsize);
  }
#endif
}
}

void HIDComposite::setup() {
  ESP_LOGI(TAG, "Setting up HID Composite (Mouse + Keyboard + Telephony)...");
  
  g_hid_composite_instance = this;

#ifdef USE_HID_COMPOSITE_LAMP_ARRAY
  this->setup_lamp_array_();
#endif

  tinyusb_config_t tusb_cfg = {
    .port = TINYUSB_PORT_FULL_SPEED_0,
    .phy = { .skip_setup = false, .self_powered = false, .vbus_monitor_io = -1, },
    .task = { .size = 4096, .priority = 5, .xCoreID = 0, },
    .descriptor = {
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
  ESP_LOGI(TAG, "TinyUSB driver installed successfully");
  this->initialized_ = true;
}

void HIDComposite::loop() {
  if (!this->initialized_) return;
  
  uint32_t now = millis();
  
  // Handle mouse keep awake
  if (this->mouse_keep_awake_enabled_) {
    if (now - this->mouse_keep_awake_last_time_ >= this->mouse_keep_awake_next_interval_) {
      // random_uint32() is the hardware RNG, so unlike rand() it does not
      // replay the same sequence on every boot.
      int8_t dx = (int8_t) (random_uint32() % 3) - 1;
      int8_t dy = (int8_t) (random_uint32() % 3) - 1;
      if (dx == 0 && dy == 0) dx = 1;
      this->move(dx, dy);
      ESP_LOGD(TAG, "Mouse keep awake: move(%d, %d)", dx, dy);
      
      this->mouse_keep_awake_next_interval_ = this->mouse_keep_awake_interval_;
      if (this->mouse_keep_awake_jitter_ > 0) {
        int32_t jitter = (int32_t) (random_uint32() % (this->mouse_keep_awake_jitter_ * 2 + 1)) -
                         (int32_t) this->mouse_keep_awake_jitter_;
        this->mouse_keep_awake_next_interval_ = (int32_t)this->mouse_keep_awake_interval_ + jitter > 1000 
                                                 ? this->mouse_keep_awake_interval_ + jitter : 1000;
      }
      this->mouse_keep_awake_last_time_ = now;
    }
  }
  
  // Handle keyboard keep awake
  if (this->keyboard_keep_awake_enabled_) {
    if (now - this->keyboard_keep_awake_last_time_ >= this->keyboard_keep_awake_next_interval_) {
      this->key_tap(this->keyboard_keep_awake_key_);
      ESP_LOGD(TAG, "Keyboard keep awake: tap(%s)", this->keyboard_keep_awake_key_.c_str());
      
      this->keyboard_keep_awake_next_interval_ = this->keyboard_keep_awake_interval_;
      if (this->keyboard_keep_awake_jitter_ > 0) {
        int32_t jitter = (int32_t) (random_uint32() % (this->keyboard_keep_awake_jitter_ * 2 + 1)) -
                         (int32_t) this->keyboard_keep_awake_jitter_;
        this->keyboard_keep_awake_next_interval_ = (int32_t)this->keyboard_keep_awake_interval_ + jitter > 1000 
                                                   ? this->keyboard_keep_awake_interval_ + jitter : 1000;
      }
      this->keyboard_keep_awake_last_time_ = now;
    }
  }

  this->type_loop_();
  this->publish_telephony_state_();

#ifdef USE_HID_COMPOSITE_LAMP_ARRAY
  this->loop_lamp_array_();
#endif
}

// The host's LED reports arrive on the TinyUSB task. Entities are published here
// instead, because publish_state() touches the API/MQTT queues and the
// scheduler, none of which are safe to use from another task.
void HIDComposite::publish_telephony_state_() {
  if (!this->led_state_dirty_.exchange(false))
    return;

  const uint8_t leds = this->last_led_report_.load();
  ESP_LOGI(TAG, "LED Report: Mute=%d, OffHook=%d, Ring=%d, Hold=%d, Mic=%d", (leds & 0x01) != 0,
           (leds & 0x02) != 0, (leds & 0x04) != 0, (leds & 0x08) != 0, (leds & 0x10) != 0);

  const bool muted = this->muted_.load();
  if (muted != this->published_muted_) {
    this->published_muted_ = muted;
    ESP_LOGI(TAG, ">>> MUTE STATE FROM TEAMS: %s <<<", muted ? "MUTED" : "UNMUTED");
    this->mute_callbacks_.call(muted);
  }

  const bool off_hook = this->off_hook_.load();
  if (off_hook != this->published_off_hook_) {
    this->published_off_hook_ = off_hook;
    ESP_LOGI(TAG, "Off-hook state changed: %s", off_hook ? "IN CALL" : "IDLE");
    this->off_hook_callbacks_.call(off_hook);
  }

  const bool ringing = this->ringing_.load();
  if (ringing != this->published_ringing_) {
    this->published_ringing_ = ringing;
    ESP_LOGI(TAG, "Ring state changed: %s", ringing ? "RINGING" : "NOT RINGING");
    this->ring_callbacks_.call(ringing);
  }

  const bool hold = this->hold_.load();
  if (hold != this->published_hold_) {
    this->published_hold_ = hold;
    ESP_LOGI(TAG, "Hold state changed: %s", hold ? "ON HOLD" : "NOT ON HOLD");
    this->hold_callbacks_.call(hold);
  }
}

void HIDComposite::dump_config() {
  ESP_LOGCONFIG(TAG, "HID Composite (Mouse + Keyboard + Telephony):");
  ESP_LOGCONFIG(TAG, "  Status: %s", this->initialized_ ? "Initialized" : "Not initialized");
  const char *layout_str = this->layout_ == LAYOUT_AZERTY_FR ? "AZERTY_FR"
                          : this->layout_ == LAYOUT_QWERTZ_DE ? "QWERTZ_DE"
                                                               : "QWERTY_US";
  ESP_LOGCONFIG(TAG, "  Keyboard layout: %s", layout_str);
#ifdef USE_HID_COMPOSITE_LAMP_ARRAY
  ESP_LOGCONFIG(TAG, "  LampArray lamps: %u (report IDs %d-%d)", this->lamp_array_.lamp_count(),
                REPORT_ID_LAMP_ARRAY_BASE, REPORT_ID_LAMP_ARRAY_BASE + lamp_array_core::LAMP_ARRAY_REPORT_COUNT - 1);
#endif
}

#ifdef USE_HID_COMPOSITE_LAMP_ARRAY

Color lamp_color_to_esphome(const lamp_array_core::LampColor &color, uint8_t intensity_levels) {
  if (intensity_levels <= 1)
    return color.intensity == 0 ? Color::BLACK : Color(color.red, color.green, color.blue);
  return Color(color.red, color.green, color.blue) * color.intensity;
}

void HIDComposite::setup_lamp_array_() {
  if (!this->lamp_array_.begin()) {
    // begin() zeroes the lamp count on failure, so the report handlers below
    // reject everything the host sends rather than indexing empty buffers.
    ESP_LOGE(TAG, "Failed to allocate LampArray state; lighting disabled");
    return;
  }
  // Sized here rather than lazily in loop(): triggers register during codegen
  // setup, which runs before any component's setup().
  if (this->want_lamp_updates_) {
    this->lamp_incoming_.assign(this->lamp_array_.lamp_count(), lamp_array_core::LampColor{0, 0, 0, 0});
    this->lamp_published_.assign(this->lamp_array_.lamp_count(), lamp_array_core::LampColor{0, 0, 0, 0});
  }
  ESP_LOGI(TAG, "LampArray ready with %u lamps", this->lamp_array_.lamp_count());
}

void HIDComposite::loop_lamp_array_() {
  // Losing the host means nobody is driving the lamps any more.
  const bool connected = this->is_connected();
  if (this->lamp_array_connected_ && !connected)
    this->lamp_array_.on_disconnect();
  this->lamp_array_connected_ = connected;

  bool autonomous;
  if (this->lamp_array_.poll_autonomous_change(&autonomous)) {
    ESP_LOGD(TAG, "LampArray autonomous mode: %s", ONOFF(autonomous));
    this->autonomous_callbacks_.call(autonomous);
  }

  // Automations run here, never inside the USB callback.
  const uint32_t frame = this->lamp_array_.frame();
  if (this->want_lamp_updates_ && frame != this->last_lamp_frame_) {
    this->last_lamp_frame_ = frame;
    this->lamp_array_.snapshot(this->lamp_incoming_.data());
    const uint8_t levels = this->lamp_array_.intensity_levels();
    for (uint16_t i = 0; i < this->lamp_incoming_.size(); i++) {
      const lamp_array_core::LampColor &color = this->lamp_incoming_[i];
      if (std::memcmp(&color, &this->lamp_published_[i], sizeof(lamp_array_core::LampColor)) == 0)
        continue;
      this->lamp_published_[i] = color;
      this->lamp_update_callbacks_.call(i, lamp_color_to_esphome(color, levels));
    }
  }
}

Color HIDComposite::get_lamp_color(uint16_t lamp_id) {
  return lamp_color_to_esphome(this->lamp_array_.get_color(lamp_id), this->lamp_array_.intensity_levels());
}

#endif  // USE_HID_COMPOSITE_LAMP_ARRAY

// ============ Mouse Functions ============

void HIDComposite::send_mouse_report() {
  if (!this->initialized_ || !tud_mounted() || !tud_hid_ready()) return;
  uint8_t report[5] = {this->mouse_buttons_, 0, 0, 0, 0};
  tud_hid_report(REPORT_ID_MOUSE, report, sizeof(report));
}

void HIDComposite::move(int8_t x, int8_t y) {
  if (!this->initialized_ || !tud_mounted() || !tud_hid_ready()) return;
  uint8_t report[5] = {this->mouse_buttons_, (uint8_t)x, (uint8_t)y, 0, 0};
  tud_hid_report(REPORT_ID_MOUSE, report, sizeof(report));
  ESP_LOGD(TAG, "Mouse move: x=%d, y=%d", x, y);
}

void HIDComposite::scroll(int8_t vertical, int8_t horizontal) {
  if (!this->initialized_ || !tud_mounted() || !tud_hid_ready()) return;
  uint8_t report[5] = {this->mouse_buttons_, 0, 0, (uint8_t)vertical, (uint8_t)horizontal};
  tud_hid_report(REPORT_ID_MOUSE, report, sizeof(report));
  ESP_LOGD(TAG, "Mouse scroll: v=%d, h=%d", vertical, horizontal);
}

void HIDComposite::click(MouseButton button) {
  this->mouse_press(button);
  this->set_timeout("click", CLICK_HOLD_MS, [this, button]() { this->mouse_release(button); });
}

void HIDComposite::mouse_press(MouseButton button) {
  this->mouse_buttons_ |= (1 << button);
  this->send_mouse_report();
  ESP_LOGD(TAG, "Mouse press: button=%d", button);
}

void HIDComposite::mouse_release(MouseButton button) {
  this->mouse_buttons_ &= ~(1 << button);
  this->send_mouse_report();
  ESP_LOGD(TAG, "Mouse release: button=%d", button);
}

void HIDComposite::mouse_release_all() {
  this->mouse_buttons_ = 0;
  this->send_mouse_report();
  ESP_LOGD(TAG, "Mouse release all");
}

// ============ Keyboard Functions ============

void HIDComposite::send_keyboard_report(uint8_t modifier, uint8_t keycode) {
  if (!this->initialized_ || !tud_mounted() || !tud_hid_ready()) return;
  uint8_t report[8] = {modifier, 0, keycode, 0, 0, 0, 0, 0};
  tud_hid_report(REPORT_ID_KEYBOARD, report, sizeof(report));
  ESP_LOGD(TAG, "Keyboard report: mod=0x%02X key=0x%02X", modifier, keycode);
}

void HIDComposite::key_press(const std::string &key, uint8_t modifier) {
  uint8_t keycode, char_mod;
  if (key.length() == 1) {
    this->char_to_keycode(key[0], keycode, char_mod);
  } else {
    keycode = this->key_name_to_keycode(key);
    char_mod = 0;
  }
  ESP_LOGD(TAG, "Key press: %s", key.c_str());
  this->send_keyboard_report(modifier | char_mod, keycode);
}

void HIDComposite::key_release() { this->key_release_all(); }
void HIDComposite::key_release_all() {
  ESP_LOGD(TAG, "Key release all");
  this->send_keyboard_report(0, 0);
}

void HIDComposite::key_tap(const std::string &key, uint8_t modifier) {
  this->key_press(key, modifier);
  this->set_timeout("key_tap", KEY_PULSE_MS, [this]() { this->key_release(); });
}

// Queues the text; type_loop_() sends it one keystroke per loop tick. Typing a
// 30-character string used to block the whole ESPHome loop for ~2 seconds.
void HIDComposite::type(const std::string &text, uint32_t speed_ms, uint32_t jitter_ms) {
  ESP_LOGI(TAG, "Type: %s (speed=%" PRIu32 "ms, jitter=%" PRIu32 "ms)", text.c_str(), speed_ms, jitter_ms);
  if (this->type_index_ >= this->type_text_.size()) {
    this->type_text_ = text;
    this->type_index_ = 0;
    this->type_next_time_ = millis();
  } else {
    // Still typing: queue behind what is in flight rather than losing it.
    this->type_text_ += text;
  }
  this->type_speed_ms_ = speed_ms;
  this->type_jitter_ms_ = jitter_ms;
}

void HIDComposite::type_loop_() {
  if (this->type_index_ >= this->type_text_.size() && !this->type_key_down_) {
    if (!this->type_text_.empty()) {
      this->type_text_.clear();
      this->type_index_ = 0;
    }
    return;
  }

  const uint32_t now = millis();
  if ((int32_t) (now - this->type_next_time_) < 0)
    return;

  if (this->type_key_down_) {
    this->send_keyboard_report(0, 0);
    this->type_key_down_ = false;

    uint32_t gap = this->type_speed_ms_;
    if (this->type_jitter_ms_ > 0) {
      const int32_t jitter = (int32_t) (random_uint32() % (this->type_jitter_ms_ * 2 + 1)) -
                             (int32_t) this->type_jitter_ms_;
      gap = (int32_t) this->type_speed_ms_ + jitter > 10 ? this->type_speed_ms_ + jitter : 10;
    }
    this->type_next_time_ = now + gap;
    return;
  }

  uint8_t keycode, mod;
  this->char_to_keycode(this->type_text_[this->type_index_++], keycode, mod);
  this->send_keyboard_report(mod, keycode);
  this->type_key_down_ = true;
  this->type_next_time_ = now + KEY_PULSE_MS;
}

// QWERTY US layout mapping
void HIDComposite::char_to_keycode_qwerty(char c, uint8_t &keycode, uint8_t &modifier) {
  modifier = 0;
  if (c >= 'a' && c <= 'z') { keycode = KEY_A + (c - 'a'); return; }
  if (c >= 'A' && c <= 'Z') { keycode = KEY_A + (c - 'A'); modifier = MOD_LEFT_SHIFT; return; }
  if (c >= '1' && c <= '9') { keycode = KEY_1 + (c - '1'); return; }
  if (c == '0') { keycode = KEY_0; return; }
  switch (c) {
    case ' ': keycode = KEY_SPACE; break;
    case '\n': keycode = KEY_ENTER; break;
    case '\t': keycode = KEY_TAB; break;
    case '-': keycode = KEY_MINUS; break;
    case '=': keycode = KEY_EQUAL; break;
    case '[': keycode = KEY_LEFT_BRACE; break;
    case ']': keycode = KEY_RIGHT_BRACE; break;
    case '\\': keycode = KEY_BACKSLASH; break;
    case ';': keycode = KEY_SEMICOLON; break;
    case '\'': keycode = KEY_APOSTROPHE; break;
    case '`': keycode = KEY_GRAVE; break;
    case ',': keycode = KEY_COMMA; break;
    case '.': keycode = KEY_PERIOD; break;
    case '/': keycode = KEY_SLASH; break;
    case '!': keycode = KEY_1; modifier = MOD_LEFT_SHIFT; break;
    case '@': keycode = KEY_2; modifier = MOD_LEFT_SHIFT; break;
    case '#': keycode = KEY_3; modifier = MOD_LEFT_SHIFT; break;
    case '$': keycode = KEY_4; modifier = MOD_LEFT_SHIFT; break;
    case '%': keycode = KEY_5; modifier = MOD_LEFT_SHIFT; break;
    case '^': keycode = KEY_6; modifier = MOD_LEFT_SHIFT; break;
    case '&': keycode = KEY_7; modifier = MOD_LEFT_SHIFT; break;
    case '*': keycode = KEY_8; modifier = MOD_LEFT_SHIFT; break;
    case '(': keycode = KEY_9; modifier = MOD_LEFT_SHIFT; break;
    case ')': keycode = KEY_0; modifier = MOD_LEFT_SHIFT; break;
    case '_': keycode = KEY_MINUS; modifier = MOD_LEFT_SHIFT; break;
    case '+': keycode = KEY_EQUAL; modifier = MOD_LEFT_SHIFT; break;
    case '{': keycode = KEY_LEFT_BRACE; modifier = MOD_LEFT_SHIFT; break;
    case '}': keycode = KEY_RIGHT_BRACE; modifier = MOD_LEFT_SHIFT; break;
    case '|': keycode = KEY_BACKSLASH; modifier = MOD_LEFT_SHIFT; break;
    case ':': keycode = KEY_SEMICOLON; modifier = MOD_LEFT_SHIFT; break;
    case '"': keycode = KEY_APOSTROPHE; modifier = MOD_LEFT_SHIFT; break;
    case '~': keycode = KEY_GRAVE; modifier = MOD_LEFT_SHIFT; break;
    case '<': keycode = KEY_COMMA; modifier = MOD_LEFT_SHIFT; break;
    case '>': keycode = KEY_PERIOD; modifier = MOD_LEFT_SHIFT; break;
    case '?': keycode = KEY_SLASH; modifier = MOD_LEFT_SHIFT; break;
    default: keycode = KEY_NONE; break;
  }
}

// AZERTY FR layout mapping
void HIDComposite::char_to_keycode_azerty(char c, uint8_t &keycode, uint8_t &modifier) {
  modifier = 0;
  
  // AZERTY swaps: A<->Q, Z<->W, M position differs
  if (c == 'a') { keycode = KEY_Q; return; }
  if (c == 'A') { keycode = KEY_Q; modifier = MOD_LEFT_SHIFT; return; }
  if (c == 'q') { keycode = KEY_A; return; }
  if (c == 'Q') { keycode = KEY_A; modifier = MOD_LEFT_SHIFT; return; }
  if (c == 'z') { keycode = KEY_W; return; }
  if (c == 'Z') { keycode = KEY_W; modifier = MOD_LEFT_SHIFT; return; }
  if (c == 'w') { keycode = KEY_Z; return; }
  if (c == 'W') { keycode = KEY_Z; modifier = MOD_LEFT_SHIFT; return; }
  if (c == 'm') { keycode = KEY_SEMICOLON; return; }
  if (c == 'M') { keycode = KEY_SEMICOLON; modifier = MOD_LEFT_SHIFT; return; }
  
  // Other letters
  if (c >= 'a' && c <= 'z') { keycode = KEY_A + (c - 'a'); return; }
  if (c >= 'A' && c <= 'Z') { keycode = KEY_A + (c - 'A'); modifier = MOD_LEFT_SHIFT; return; }
  
  // Numbers on AZERTY require Shift
  if (c >= '1' && c <= '9') { keycode = KEY_1 + (c - '1'); modifier = MOD_LEFT_SHIFT; return; }
  if (c == '0') { keycode = KEY_0; modifier = MOD_LEFT_SHIFT; return; }
  
  switch (c) {
    case ' ': keycode = KEY_SPACE; break;
    case '\n': keycode = KEY_ENTER; break;
    case '\t': keycode = KEY_TAB; break;
    case '&': keycode = KEY_1; break;
    case '-': keycode = KEY_6; break;
    case '_': keycode = KEY_8; break;
    case '.': keycode = KEY_COMMA; modifier = MOD_LEFT_SHIFT; break;
    case ',': keycode = KEY_M; break;
    case ';': keycode = KEY_COMMA; break;
    case ':': keycode = KEY_PERIOD; break;
    case '!': keycode = KEY_SLASH; break;
    case '?': keycode = KEY_M; modifier = MOD_LEFT_SHIFT; break;
    case '/': keycode = KEY_PERIOD; modifier = MOD_LEFT_SHIFT; break;
    case '*': keycode = KEY_BACKSLASH; break;
    case '(': keycode = KEY_5; break;
    case ')': keycode = KEY_MINUS; break;
    case '=': keycode = KEY_EQUAL; break;
    case '+': keycode = KEY_EQUAL; modifier = MOD_LEFT_SHIFT; break;
    default: keycode = KEY_NONE; break;
  }
}

// QWERTZ DE layout mapping
void HIDComposite::char_to_keycode_qwertz(char c, uint8_t &keycode, uint8_t &modifier) {
  modifier = 0;
  
  // QWERTZ swaps Y<->Z
  if (c == 'y') { keycode = KEY_Z; return; }
  if (c == 'Y') { keycode = KEY_Z; modifier = MOD_LEFT_SHIFT; return; }
  if (c == 'z') { keycode = KEY_Y; return; }
  if (c == 'Z') { keycode = KEY_Y; modifier = MOD_LEFT_SHIFT; return; }
  
  if (c >= 'a' && c <= 'z') { keycode = KEY_A + (c - 'a'); return; }
  if (c >= 'A' && c <= 'Z') { keycode = KEY_A + (c - 'A'); modifier = MOD_LEFT_SHIFT; return; }
  if (c >= '1' && c <= '9') { keycode = KEY_1 + (c - '1'); return; }
  if (c == '0') { keycode = KEY_0; return; }
  
  switch (c) {
    case ' ': keycode = KEY_SPACE; break;
    case '\n': keycode = KEY_ENTER; break;
    case '\t': keycode = KEY_TAB; break;
    case '-': keycode = KEY_SLASH; break;
    case '_': keycode = KEY_SLASH; modifier = MOD_LEFT_SHIFT; break;
    case '.': keycode = KEY_PERIOD; break;
    case ',': keycode = KEY_COMMA; break;
    case ';': keycode = KEY_COMMA; modifier = MOD_LEFT_SHIFT; break;
    case ':': keycode = KEY_PERIOD; modifier = MOD_LEFT_SHIFT; break;
    case '?': keycode = KEY_MINUS; modifier = MOD_LEFT_SHIFT; break;
    case '!': keycode = KEY_1; modifier = MOD_LEFT_SHIFT; break;
    case '/': keycode = KEY_7; modifier = MOD_LEFT_SHIFT; break;
    case '(': keycode = KEY_8; modifier = MOD_LEFT_SHIFT; break;
    case ')': keycode = KEY_9; modifier = MOD_LEFT_SHIFT; break;
    case '=': keycode = KEY_0; modifier = MOD_LEFT_SHIFT; break;
    case '+': keycode = KEY_RIGHT_BRACE; break;
    case '*': keycode = KEY_RIGHT_BRACE; modifier = MOD_LEFT_SHIFT; break;
    default: keycode = KEY_NONE; break;
  }
}

// Main dispatcher based on layout
void HIDComposite::char_to_keycode(char c, uint8_t &keycode, uint8_t &modifier) {
  switch (this->layout_) {
    case LAYOUT_AZERTY_FR:
      this->char_to_keycode_azerty(c, keycode, modifier);
      break;
    case LAYOUT_QWERTZ_DE:
      this->char_to_keycode_qwertz(c, keycode, modifier);
      break;
    case LAYOUT_QWERTY_US:
    default:
      this->char_to_keycode_qwerty(c, keycode, modifier);
      break;
  }
}

uint8_t HIDComposite::key_name_to_keycode(const std::string &key) {
  std::string k = key;
  for (char &c : k) if (c >= 'a' && c <= 'z') c -= 32;
  if (k == "ENTER" || k == "RETURN") return KEY_ENTER;
  if (k == "ESC" || k == "ESCAPE") return KEY_ESC;
  if (k == "BACKSPACE") return KEY_BACKSPACE;
  if (k == "TAB") return KEY_TAB;
  if (k == "SPACE") return KEY_SPACE;
  if (k == "DELETE") return KEY_DELETE;
  if (k == "INSERT") return KEY_INSERT;
  if (k == "HOME") return KEY_HOME;
  if (k == "END") return KEY_END;
  if (k == "PAGEUP") return KEY_PAGE_UP;
  if (k == "PAGEDOWN") return KEY_PAGE_DOWN;
  if (k == "UP") return KEY_UP_ARROW;
  if (k == "DOWN") return KEY_DOWN_ARROW;
  if (k == "LEFT") return KEY_LEFT_ARROW;
  if (k == "RIGHT") return KEY_RIGHT_ARROW;
  if (k == "F1") return KEY_F1; if (k == "F2") return KEY_F2; if (k == "F3") return KEY_F3;
  if (k == "F4") return KEY_F4; if (k == "F5") return KEY_F5; if (k == "F6") return KEY_F6;
  if (k == "F7") return KEY_F7; if (k == "F8") return KEY_F8; if (k == "F9") return KEY_F9;
  if (k == "F10") return KEY_F10; if (k == "F11") return KEY_F11; if (k == "F12") return KEY_F12;
  if (k == "F13") return KEY_F13; if (k == "F14") return KEY_F14; if (k == "F15") return KEY_F15;
  if (k == "F16") return KEY_F16; if (k == "F17") return KEY_F17; if (k == "F18") return KEY_F18;
  if (k == "F19") return KEY_F19; if (k == "F20") return KEY_F20; if (k == "F21") return KEY_F21;
  if (k == "F22") return KEY_F22; if (k == "F23") return KEY_F23; if (k == "F24") return KEY_F24;
  ESP_LOGW(TAG, "Unknown key: %s", key.c_str());
  return KEY_NONE;
}

void HIDComposite::start_mouse_keep_awake(uint32_t interval_ms, uint32_t jitter_ms) {
  ESP_LOGI(TAG, "Starting mouse keep awake: interval=%" PRIu32 "ms, jitter=%" PRIu32 "ms", interval_ms, jitter_ms);
  this->mouse_keep_awake_interval_ = interval_ms;
  this->mouse_keep_awake_jitter_ = jitter_ms;
  this->mouse_keep_awake_last_time_ = millis();
  this->mouse_keep_awake_next_interval_ = interval_ms;
  this->mouse_keep_awake_enabled_ = true;
}

void HIDComposite::stop_mouse_keep_awake() {
  ESP_LOGI(TAG, "Stopping mouse keep awake");
  this->mouse_keep_awake_enabled_ = false;
}

void HIDComposite::start_keyboard_keep_awake(const std::string &key, uint32_t interval_ms, uint32_t jitter_ms) {
  ESP_LOGI(TAG, "Starting keyboard keep awake: key=%s, interval=%" PRIu32 "ms, jitter=%" PRIu32 "ms", key.c_str(), interval_ms, jitter_ms);
  this->keyboard_keep_awake_key_ = key;
  this->keyboard_keep_awake_interval_ = interval_ms;
  this->keyboard_keep_awake_jitter_ = jitter_ms;
  this->keyboard_keep_awake_last_time_ = millis();
  this->keyboard_keep_awake_next_interval_ = interval_ms;
  this->keyboard_keep_awake_enabled_ = true;
}

void HIDComposite::stop_keyboard_keep_awake() {
  ESP_LOGI(TAG, "Stopping keyboard keep awake");
  this->keyboard_keep_awake_enabled_ = false;
}

bool HIDComposite::is_connected() {
  if (!this->initialized_) return false;
  // tud_mounted() alone is not enough when behind a hub:
  // the hub may keep the device enumerated even when the PC is disconnected.
  // tud_suspended() detects when the host stops sending SOF frames (~3ms),
  // which happens when the PC is disconnected from the hub.
  return tud_mounted() && !tud_suspended();
}

bool HIDComposite::is_ready() {
  if (!this->initialized_) return false;
  return tud_mounted() && !tud_suspended() && tud_hid_ready();
}

// ============ Telephony Functions (Poly BT700 Compatible) ============

// Optimistically updates the cached mute state and fires the callback right
// away. Safe to call directly (unlike process_host_report()) because every
// caller runs from action/loop context, never the TinyUSB task.
void HIDComposite::set_muted_(bool muted) {
  this->muted_.store(muted);
  this->host_state_known_.store(true);
  if (muted != this->published_muted_) {
    this->published_muted_ = muted;
    this->mute_callbacks_.call(muted);
  }
}

// Presses and releases the mute button. The press/release gap is scheduled
// rather than delay()ed, so the ESPHome loop keeps running.
void HIDComposite::send_mute_pulse_(bool telephony, bool consumer) {
  this->mute_button_ = true;
  if (telephony)
    this->send_telephony_report();
  if (consumer)
    this->mute_consumer();

  this->set_timeout("mute_release", MUTE_PULSE_MS, [this, telephony]() {
    this->mute_button_ = false;
    if (telephony)
      this->send_telephony_report();
  });
}

// The mute button is a toggle on the wire, so an absolute request only presses
// it when the host's reported state disagrees. Until the host has sent an LED
// report there is nothing to compare against, so press and let it tell us.
void HIDComposite::set_mute(bool state) {
  if (this->host_state_known_.load() && this->muted_.load() == state) {
    ESP_LOGD(TAG, "Already %s, nothing to send", state ? "muted" : "unmuted");
    return;
  }
  ESP_LOGI(TAG, "Requesting %s", state ? "mute" : "unmute");
  this->send_mute_pulse_(true, false);
  // Assume the request took effect. Hosts that echo the Telephony LED correct
  // us for real in process_host_report(); hosts that never send one would
  // otherwise leave muted_ stuck at whatever it was before.
  this->set_muted_(state);
}

void HIDComposite::mute() { this->set_mute(true); }

void HIDComposite::unmute() { this->set_mute(false); }

void HIDComposite::toggle_mute() {
  ESP_LOGI(TAG, "Toggling mute (Poly BT700 format)");
  this->send_mute_pulse_(true, false);
  // Assume the toggle took effect, same reasoning as set_mute() above.
  this->set_muted_(!this->muted_.load());
}

void HIDComposite::hook_switch(bool state) {
  ESP_LOGD(TAG, "Hook switch: %s", state ? "ON" : "OFF");
  this->hook_button_ = state;
  this->send_telephony_report();
}

void HIDComposite::answer_call() {
  ESP_LOGI(TAG, "Answering call");
  this->hook_button_ = true;
  this->send_telephony_report();
}

void HIDComposite::hang_up() {
  ESP_LOGI(TAG, "Hanging up");
  this->hook_button_ = false;
  this->send_telephony_report();
}

void HIDComposite::send_telephony_report() {
  if (!this->initialized_ || !tud_mounted() || !tud_hid_ready()) return;
  
  // Poly BT700 compatible format:
  // bit 0 = Hook Switch (No Preferred)
  // bit 1 = Phone Mute (RELATIVE!)
  // bit 2 = Flash
  // bit 3 = Redial
  // bit 4 = Button 7
  // bits 5-7 = padding
  uint8_t report = 0;
  if (this->hook_button_) report |= 0x01;
  if (this->mute_button_) report |= 0x02;
  
  tud_hid_report(REPORT_ID_TELEPHONY_INPUT, &report, sizeof(report));
  
  ESP_LOGD(TAG, "Sent telephony report (0x20): hook=%d, mute=%d", this->hook_button_, this->mute_button_);
}

void HIDComposite::mute_telephony() {
  ESP_LOGI(TAG, "Sending Telephony mute (Poly BT700 compatible)");
  // RELATIVE input: a press then a release, scheduled so the loop keeps running.
  this->send_mute_pulse_(true, false);
  // Assume the toggle took effect, same as toggle_mute() - otherwise a host
  // that never echoes the Telephony LED leaves muted_ stuck forever.
  this->set_muted_(!this->muted_.load());
}

// Sends one Consumer Control button as a press then a scheduled release.
void HIDComposite::send_consumer_pulse_(uint8_t bit, const char *name) {
  if (!this->initialized_ || !tud_mounted() || !tud_hid_ready()) return;

  ESP_LOGD(TAG, "Sending Consumer %s", name);
  uint8_t report = bit;
  tud_hid_report(REPORT_ID_CONSUMER, &report, sizeof(report));
  this->set_timeout("consumer_release", CONSUMER_PULSE_MS, [this]() {
    if (!tud_mounted() || !tud_hid_ready()) return;
    uint8_t release = 0x00;
    tud_hid_report(REPORT_ID_CONSUMER, &release, sizeof(release));
  });
}

void HIDComposite::mute_consumer() {
  ESP_LOGI(TAG, "Sending Consumer Mute (system volume mute)");
  this->send_consumer_pulse_(0x01, "Mute");
  this->set_muted_(!this->muted_.load());
}

void HIDComposite::mute_teams() {
  // Teams shortcut: Ctrl+Shift+M (Windows) or Cmd+Shift+M (Mac)
  ESP_LOGI(TAG, "Sending Teams mute shortcut (Ctrl+Shift+M)");

  this->send_keyboard_report(MOD_LEFT_CTRL | MOD_LEFT_SHIFT, KEY_M);
  this->set_timeout("teams_release", KEY_PULSE_MS, [this]() { this->send_keyboard_report(0, 0); });
  this->set_muted_(!this->muted_.load());
}

void HIDComposite::volume_up() { this->send_consumer_pulse_(0x02, "Volume Up"); }

void HIDComposite::volume_down() { this->send_consumer_pulse_(0x04, "Volume Down"); }

void HIDComposite::process_host_report(uint8_t report_id, uint8_t const *buffer, uint16_t bufsize) {
  if (bufsize < 1) return;
  
  ESP_LOGD(TAG, ">>> Received host report: ID=0x%02X, size=%d, data=0x%02X", report_id, bufsize, buffer[0]);
  
  if (report_id == REPORT_ID_TELEPHONY_LED) {
    // LED report format:
    // bit 0 = Mute LED
    // bit 1 = Off-Hook LED
    // bit 2 = Ring LED
    // bit 3 = Hold LED
    // bit 4 = Microphone LED
    uint8_t leds = buffer[0];

    const bool new_mic = (leds & 0x10) != 0;

    // Cache only; publish_telephony_state_() fires the automations from loop(),
    // because this runs on the TinyUSB task.
    this->muted_.store(((leds & 0x01) != 0) || new_mic);
    this->off_hook_.store((leds & 0x02) != 0);
    this->ringing_.store((leds & 0x04) != 0);
    this->hold_.store((leds & 0x08) != 0);
    this->host_state_known_.store(true);
    this->last_led_report_.store(leds);
    this->led_state_dirty_.store(true);
  } else if (report_id == REPORT_ID_KEYBOARD) {
    // Keyboard LED report (Num Lock, Caps Lock, etc)
    ESP_LOGD(TAG, "Keyboard LED report: 0x%02X", buffer[0]);
  } else {
    ESP_LOGD(TAG, "Unknown report ID: 0x%02X", report_id);
  }
}

}  // namespace hid_composite
}  // namespace esphome

#else

namespace esphome {
namespace hid_composite {
static const char *const TAG = "hid_composite";
void HIDComposite::setup() { ESP_LOGE(TAG, "Only supported on ESP32 chips with USB OTG (S2/S3/P4)"); }
void HIDComposite::loop() {}
void HIDComposite::dump_config() {}
void HIDComposite::move(int8_t x, int8_t y) {}
void HIDComposite::scroll(int8_t vertical, int8_t horizontal) {}
void HIDComposite::click(MouseButton button) {}
void HIDComposite::mouse_press(MouseButton button) {}
void HIDComposite::mouse_release(MouseButton button) {}
void HIDComposite::mouse_release_all() {}
void HIDComposite::key_press(const std::string &key, uint8_t modifier) {}
void HIDComposite::key_release() {}
void HIDComposite::key_release_all() {}
void HIDComposite::key_tap(const std::string &key, uint8_t modifier) {}
void HIDComposite::type(const std::string &text, uint32_t speed_ms, uint32_t jitter_ms) {}
void HIDComposite::char_to_keycode(char c, uint8_t &keycode, uint8_t &modifier) {}
void HIDComposite::char_to_keycode_qwerty(char c, uint8_t &keycode, uint8_t &modifier) {}
void HIDComposite::char_to_keycode_azerty(char c, uint8_t &keycode, uint8_t &modifier) {}
void HIDComposite::char_to_keycode_qwertz(char c, uint8_t &keycode, uint8_t &modifier) {}
uint8_t HIDComposite::key_name_to_keycode(const std::string &key) { return 0; }
void HIDComposite::send_mouse_report() {}
void HIDComposite::send_keyboard_report(uint8_t modifier, uint8_t keycode) {}
void HIDComposite::start_mouse_keep_awake(uint32_t interval_ms, uint32_t jitter_ms) {}
void HIDComposite::stop_mouse_keep_awake() {}
void HIDComposite::start_keyboard_keep_awake(const std::string &key, uint32_t interval_ms, uint32_t jitter_ms) {}
void HIDComposite::stop_keyboard_keep_awake() {}
bool HIDComposite::is_connected() { return false; }
bool HIDComposite::is_ready() { return false; }
void HIDComposite::mute() {}
void HIDComposite::unmute() {}
void HIDComposite::toggle_mute() {}
void HIDComposite::mute_telephony() {}
void HIDComposite::mute_consumer() {}
void HIDComposite::mute_teams() {}
void HIDComposite::volume_up() {}
void HIDComposite::volume_down() {}
void HIDComposite::hook_switch(bool state) {}
void HIDComposite::answer_call() {}
void HIDComposite::hang_up() {}
void HIDComposite::send_telephony_report() {}
void HIDComposite::set_mute(bool state) {}
void HIDComposite::set_muted_(bool muted) {}
void HIDComposite::send_mute_pulse_(bool telephony, bool consumer) {}
void HIDComposite::send_consumer_pulse_(uint8_t bit, const char *name) {}
void HIDComposite::publish_telephony_state_() {}
void HIDComposite::type_loop_() {}
void HIDComposite::process_host_report(uint8_t report_id, uint8_t const *buffer, uint16_t bufsize) {}
#ifdef USE_HID_COMPOSITE_LAMP_ARRAY
Color lamp_color_to_esphome(const lamp_array_core::LampColor &color, uint8_t intensity_levels) { return Color::BLACK; }
void HIDComposite::setup_lamp_array_() {}
void HIDComposite::loop_lamp_array_() {}
Color HIDComposite::get_lamp_color(uint16_t lamp_id) { return Color::BLACK; }
#endif
}  // namespace hid_composite
}  // namespace esphome

#endif
#endif
