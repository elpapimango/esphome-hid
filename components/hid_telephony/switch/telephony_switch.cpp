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
  // mute()/unmute() no-op if already in the requested state, so this is safe
  // to call even if a previous toggle never got confirmed by the host. Publish
  // here rather than relying solely on the mute callback: mute()/unmute() can
  // legitimately no-op (state already matches), and then the callback never
  // fires, which would otherwise leave the switch stuck on the old state. A
  // later host LED report, if any, still corrects this through the callback.
  if (state) {
    this->parent_->mute();
  } else {
    this->parent_->unmute();
  }
  this->publish_state(state);
}

}  // namespace hid_telephony
}  // namespace esphome
