#include "telephony_switch.h"
#include "esphome/core/log.h"

namespace esphome {
namespace hid_telephony {

static const char *const TAG = "hid_telephony.switch";

void MuteSwitch::setup() {
  // Initialize with current state from parent
  this->publish_state(this->parent_->is_muted());
  
  // Register callback to update switch state when PC changes mute
  this->parent_->add_on_mute_callback([this](bool muted) {
    this->publish_state(muted);
  });
}

void MuteSwitch::dump_config() {
  LOG_SWITCH("", "HID Telephony Mute Switch", this);
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

}  // namespace hid_telephony
}  // namespace esphome
