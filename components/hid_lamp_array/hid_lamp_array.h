#pragma once

#include "esphome/core/automation.h"
#include "esphome/core/color.h"
#include "esphome/core/component.h"

#ifdef USE_ESP32

#include <functional>
#include <vector>

#include "lamp_array_core.h"

// Chips with native USB OTG (ESP32-S2, S3, P4). Ask the SoC caps header rather
// than listing targets, so every HID component agrees on the same test.
#include <soc/soc_caps.h>
#if SOC_USB_OTG_SUPPORTED
#define HID_LAMP_ARRAY_SUPPORTED
#endif

namespace esphome {
namespace hid_lamp_array {

using lamp_array_core::LampArrayCore;
using lamp_array_core::LampColor;

// USB HID LampArray device: the PC pushes per-lamp colours to us (Windows
// Dynamic Lighting, or anything using Windows.Devices.Lights).
class HIDLampArray : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

  // Protocol state, configured from codegen and read by the light effect.
  LampArrayCore &core() { return this->core_; }

  uint16_t lamp_count() const { return this->core_.lamp_count(); }
  // True while the host has not claimed control, so the device should render
  // its own lighting.
  bool is_autonomous() const { return this->core_.is_autonomous(); }
  bool is_connected();
  bool is_ready();

  // Host-set colour of one lamp, with the intensity channel already folded in.
  Color get_color(uint16_t lamp_id);

  void add_on_lamp_update_callback(std::function<void(uint16_t, Color)> &&callback) {
    this->lamp_update_callbacks_.add(std::move(callback));
    this->want_lamp_updates_ = true;
  }
  void add_on_autonomous_mode_callback(std::function<void(bool)> &&callback) {
    this->autonomous_callbacks_.add(std::move(callback));
  }

 protected:
  LampArrayCore core_;
  bool initialized_{false};
  bool was_connected_{false};

  // Only diff frames when something actually listens for per-lamp updates.
  bool want_lamp_updates_{false};
  uint32_t last_frame_{0};
  std::vector<LampColor> incoming_;
  std::vector<LampColor> published_;

  CallbackManager<void(uint16_t, Color)> lamp_update_callbacks_;
  CallbackManager<void(bool)> autonomous_callbacks_;
};

// Global instance for the TinyUSB callbacks.
extern HIDLampArray *g_hid_lamp_array_instance;

// Folds the LampArray intensity channel into an RGB colour. With a single
// intensity level the channel is a simple on/off gate, which is what Windows
// expects from the Microsoft reference devices; with more levels it scales.
Color lamp_color_to_esphome(const LampColor &color, uint8_t intensity_levels);

class LampUpdateTrigger : public Trigger<uint16_t, Color> {
 public:
  explicit LampUpdateTrigger(HIDLampArray *parent) {
    parent->add_on_lamp_update_callback([this](uint16_t lamp_id, Color color) { this->trigger(lamp_id, color); });
  }
};

class AutonomousModeTrigger : public Trigger<bool> {
 public:
  explicit AutonomousModeTrigger(HIDLampArray *parent) {
    parent->add_on_autonomous_mode_callback([this](bool autonomous) { this->trigger(autonomous); });
  }
};

}  // namespace hid_lamp_array
}  // namespace esphome

#endif  // USE_ESP32
