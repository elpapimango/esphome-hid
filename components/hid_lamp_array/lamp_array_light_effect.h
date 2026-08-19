#pragma once

#include "esphome/core/defines.h"

#ifdef USE_LIGHT

#include <vector>

#include "esphome/components/light/addressable_light_effect.h"
#include "hid_lamp_array.h"

#ifdef USE_ESP32

namespace esphome {
namespace hid_lamp_array {

// Renders the host-driven LampArray state onto an addressable light, the same
// way the e131 component renders a DMX universe.
//
// While the host has not claimed control (autonomous mode) the strip shows the
// light's own colour, so Home Assistant still governs it and the effect can be
// left selected permanently.
class LampArrayLightEffect : public light::AddressableLightEffect {
 public:
  explicit LampArrayLightEffect(const char *name) : AddressableLightEffect(name) {}

  void set_lamp_array(HIDLampArray *lamp_array) { this->lamp_array_ = lamp_array; }
  // Lamp id rendered on the first LED of this light, so several strips can
  // share one LampArray.
  void set_offset(uint16_t offset) { this->offset_ = offset; }

  void start() override;
  void apply(light::AddressableLight &it, const Color &current_color) override;

 protected:
  HIDLampArray *lamp_array_{nullptr};
  uint16_t offset_{0};
  std::vector<LampColor> buffer_;
  uint32_t last_frame_{0};
  bool painted_autonomous_{false};
  Color last_autonomous_color_{Color::BLACK};
};

}  // namespace hid_lamp_array
}  // namespace esphome

#endif  // USE_ESP32
#endif  // USE_LIGHT
