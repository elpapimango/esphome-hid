#include "lamp_array_binary_sensor.h"
#include "esphome/core/log.h"

#ifdef USE_ESP32

namespace esphome {
namespace hid_lamp_array {

static const char *const TAG = "hid_lamp_array.binary_sensor";

void LampArrayConnectedBinarySensor::dump_config() { LOG_BINARY_SENSOR("", "HID LampArray Connected", this); }

void LampArrayConnectedBinarySensor::update() { this->publish_state(this->parent_->is_connected()); }

void LampArrayAutonomousBinarySensor::dump_config() { LOG_BINARY_SENSOR("", "HID LampArray Autonomous", this); }

void LampArrayAutonomousBinarySensor::setup() {
  this->parent_->add_on_autonomous_mode_callback([this](bool autonomous) { this->publish_state(autonomous); });
  // Devices start out autonomous until the host claims control.
  this->publish_state(this->parent_->is_autonomous());
}

}  // namespace hid_lamp_array
}  // namespace esphome

#endif  // USE_ESP32
