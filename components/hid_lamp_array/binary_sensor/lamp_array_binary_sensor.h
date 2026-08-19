#pragma once

#include "esphome/components/binary_sensor/binary_sensor.h"
#include "esphome/core/component.h"
#include "../hid_lamp_array.h"

#ifdef USE_ESP32

namespace esphome {
namespace hid_lamp_array {

// PC connected over USB (polling)
class LampArrayConnectedBinarySensor : public binary_sensor::BinarySensor, public PollingComponent {
 public:
  void setup() override {}
  void update() override;
  void dump_config() override;

  void set_parent(HIDLampArray *parent) { this->parent_ = parent; }

 protected:
  HIDLampArray *parent_{nullptr};
};

// True while the host has not claimed the lamps, so device-side effects own
// them (callback-based).
class LampArrayAutonomousBinarySensor : public binary_sensor::BinarySensor, public Component {
 public:
  void setup() override;
  void dump_config() override;

  void set_parent(HIDLampArray *parent) { this->parent_ = parent; }

 protected:
  HIDLampArray *parent_{nullptr};
};

}  // namespace hid_lamp_array
}  // namespace esphome

#endif  // USE_ESP32
