#include "keep_awake_switch.h"
#include "esphome/core/log.h"
#include <cinttypes>

namespace esphome {
namespace hid_composite {

static const char *const TAG = "hid_composite.switch";

// Mouse Keep Awake Switch
void MouseKeepAwakeSwitch::dump_config() {
  LOG_SWITCH("", "HID Composite Mouse Keep Awake Switch", this);
  ESP_LOGCONFIG(TAG, "  Interval: %" PRIu32 "ms", this->interval_);
  ESP_LOGCONFIG(TAG, "  Jitter: %" PRIu32 "ms", this->jitter_);
}

void MouseKeepAwakeSwitch::write_state(bool state) {
  if (state) {
    this->parent_->start_mouse_keep_awake(this->interval_, this->jitter_);
  } else {
    this->parent_->stop_mouse_keep_awake();
  }
  this->publish_state(state);
}

// Keyboard Keep Awake Switch
void KeyboardKeepAwakeSwitch::dump_config() {
  LOG_SWITCH("", "HID Composite Keyboard Keep Awake Switch", this);
  ESP_LOGCONFIG(TAG, "  Key: %s", this->key_.c_str());
  ESP_LOGCONFIG(TAG, "  Interval: %" PRIu32 "ms", this->interval_);
  ESP_LOGCONFIG(TAG, "  Jitter: %" PRIu32 "ms", this->jitter_);
}

void KeyboardKeepAwakeSwitch::write_state(bool state) {
  if (state) {
    this->parent_->start_keyboard_keep_awake(this->key_, this->interval_, this->jitter_);
  } else {
    this->parent_->stop_keyboard_keep_awake();
  }
  this->publish_state(state);
}

// Mute Switch
void MuteSwitch::setup() {
  // Initialize with current state from parent
  this->publish_state(this->parent_->is_muted());
  
  // Register callback to update switch state when PC changes mute
  this->parent_->add_on_mute_callback([this](bool muted) {
    this->publish_state(muted);
  });
}

void MuteSwitch::dump_config() {
  LOG_SWITCH("", "HID Composite Mute Switch", this);
}

void MuteSwitch::write_state(bool state) {
  // Drive the PC towards the requested state. set_mute() sends nothing when the
  // PC already reports that state, so turn_on/turn_off stay idempotent instead
  // of blind-toggling (which used to unmute a call when asked to mute it).
  this->parent_->set_mute(state);

  // Publish our best knowledge now so the UI is never stuck: once the PC sends
  // an LED report, the mute callback publishes the confirmed state over this.
  this->publish_state(this->parent_->host_state_known() ? this->parent_->is_muted() : state);
}

}  // namespace hid_composite
}  // namespace esphome
