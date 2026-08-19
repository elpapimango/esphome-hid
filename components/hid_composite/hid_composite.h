#pragma once

#include "esphome/core/automation.h"
#include "esphome/core/color.h"
#include "esphome/core/component.h"
#include "esphome/core/helpers.h"

#ifdef USE_ESP32
#include <soc/soc_caps.h>
#if SOC_USB_OTG_SUPPORTED
#define HID_COMPOSITE_SUPPORTED
#endif
#endif

#include <atomic>

#ifdef USE_HID_COMPOSITE_LAMP_ARRAY
#include <vector>
#include "lamp_array_core.h"
#endif

namespace esphome {
namespace hid_composite {

// Keyboard layouts
enum KeyboardLayout : uint8_t {
  LAYOUT_QWERTY_US = 0,
  LAYOUT_AZERTY_FR = 1,
  LAYOUT_QWERTZ_DE = 2,
};

enum MouseButton : uint8_t {
  BUTTON_LEFT = 0,
  BUTTON_RIGHT = 1,
  BUTTON_MIDDLE = 2,
};

enum Modifier : uint8_t {
  MOD_NONE = 0x00,
  MOD_LEFT_CTRL = 0x01,
  MOD_LEFT_SHIFT = 0x02,
  MOD_LEFT_ALT = 0x04,
  MOD_LEFT_GUI = 0x08,
  MOD_RIGHT_CTRL = 0x10,
  MOD_RIGHT_SHIFT = 0x20,
  MOD_RIGHT_ALT = 0x40,
  MOD_RIGHT_GUI = 0x80,
};

class HIDComposite : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;

  // Mouse functions
  void move(int8_t x, int8_t y);
  void scroll(int8_t vertical, int8_t horizontal);
  void click(MouseButton button);
  void mouse_press(MouseButton button);
  void mouse_release(MouseButton button);
  void mouse_release_all();

  // Keyboard functions
  void key_press(const std::string &key, uint8_t modifier = 0);
  void key_release();
  void key_release_all();
  void key_tap(const std::string &key, uint8_t modifier = 0);
  void type(const std::string &text, uint32_t speed_ms = 50, uint32_t jitter_ms = 0);
  
  // Layout
  void set_layout(KeyboardLayout layout) { this->layout_ = layout; }
  KeyboardLayout get_layout() const { return this->layout_; }
  
  // Keep awake (mouse)
  void start_mouse_keep_awake(uint32_t interval_ms, uint32_t jitter_ms = 0);
  void stop_mouse_keep_awake();
  
  // Keep awake (keyboard)
  void start_keyboard_keep_awake(const std::string &key, uint32_t interval_ms, uint32_t jitter_ms = 0);
  void stop_keyboard_keep_awake();
  
  // Connection status
  bool is_connected();
  bool is_ready();
  
  // Telephony functions. The mute button is a toggle on the wire, so mute() and
  // unmute() drive the PC towards the requested state using the mute state it
  // last reported; toggle_mute() always sends a press.
  void mute();
  void unmute();
  void toggle_mute();
  void set_mute(bool state);
  void mute_telephony();   // Envoie uniquement le rapport Telephony (0x0B)
  void mute_consumer();    // Envoie Consumer Mute (system volume)
  void mute_teams();       // Envoie Ctrl+Shift+M (Teams shortcut)
  void hook_switch(bool state);
  void answer_call();
  void hang_up();
  
  // Volume control (Consumer Control)
  void volume_up();
  void volume_down();
  
  // Telephony state getters. Written from the TinyUSB task, read from the loop.
  bool is_muted() { return this->muted_.load(); }
  bool is_off_hook() { return this->off_hook_.load(); }
  bool is_ringing() { return this->ringing_.load(); }
  bool is_hold() { return this->hold_.load(); }
  // True once the host has sent an LED report, i.e. the states above reflect
  // the PC rather than our defaults.
  bool host_state_known() const { return this->host_state_known_.load(); }
  
  // Telephony callbacks
  void add_on_mute_callback(std::function<void(bool)> &&callback) { this->mute_callbacks_.add(std::move(callback)); }
  void add_on_off_hook_callback(std::function<void(bool)> &&callback) { this->off_hook_callbacks_.add(std::move(callback)); }
  void add_on_ring_callback(std::function<void(bool)> &&callback) { this->ring_callbacks_.add(std::move(callback)); }
  void add_on_hold_callback(std::function<void(bool)> &&callback) { this->hold_callbacks_.add(std::move(callback)); }
  
  // Process host report (for telephony LED states) - Poly BT700 format with separate Report IDs
  void process_host_report(uint8_t report_id, uint8_t const *buffer, uint16_t bufsize);

#ifdef USE_HID_COMPOSITE_LAMP_ARRAY
  // ============ LampArray (host-driven lighting) ============
  // Protocol state, configured from codegen and read by the light effect.
  lamp_array_core::LampArrayCore &core() { return this->lamp_array_; }

  uint16_t lamp_count() const { return this->lamp_array_.lamp_count(); }
  // True while the host has not claimed the lamps.
  bool is_autonomous() const { return this->lamp_array_.is_autonomous(); }
  // Host-set colour of one lamp, with the intensity channel folded in.
  Color get_lamp_color(uint16_t lamp_id);

  void add_on_lamp_update_callback(std::function<void(uint16_t, Color)> &&callback) {
    this->lamp_update_callbacks_.add(std::move(callback));
    this->want_lamp_updates_ = true;
  }
  void add_on_autonomous_mode_callback(std::function<void(bool)> &&callback) {
    this->autonomous_callbacks_.add(std::move(callback));
  }
#endif

 protected:
  bool initialized_{false};
  KeyboardLayout layout_{LAYOUT_QWERTY_US};
  uint8_t mouse_buttons_{0};

  void send_mouse_report();
  void send_keyboard_report(uint8_t modifier, uint8_t keycode);
  void char_to_keycode(char c, uint8_t &keycode, uint8_t &modifier);
  void char_to_keycode_qwerty(char c, uint8_t &keycode, uint8_t &modifier);
  void char_to_keycode_azerty(char c, uint8_t &keycode, uint8_t &modifier);
  void char_to_keycode_qwertz(char c, uint8_t &keycode, uint8_t &modifier);
  uint8_t key_name_to_keycode(const std::string &key);
  
  // Mouse keep awake state
  bool mouse_keep_awake_enabled_{false};
  uint32_t mouse_keep_awake_interval_{60000};
  uint32_t mouse_keep_awake_jitter_{0};
  uint32_t mouse_keep_awake_last_time_{0};
  uint32_t mouse_keep_awake_next_interval_{0};
  
  // Keyboard keep awake state
  bool keyboard_keep_awake_enabled_{false};
  std::string keyboard_keep_awake_key_;
  uint32_t keyboard_keep_awake_interval_{60000};
  uint32_t keyboard_keep_awake_jitter_{0};
  uint32_t keyboard_keep_awake_last_time_{0};
  uint32_t keyboard_keep_awake_next_interval_{0};
  
  // Telephony state the host reports via LED output reports. Written on the
  // TinyUSB task, so these are atomic and the automations run from loop().
  std::atomic<bool> muted_{false};
  std::atomic<bool> off_hook_{false};
  std::atomic<bool> ringing_{false};
  std::atomic<bool> hold_{false};
  std::atomic<bool> host_state_known_{false};
  std::atomic<bool> led_state_dirty_{false};
  // Raw byte from the last LED report, kept so loop() can log it verbatim.
  std::atomic<uint8_t> last_led_report_{0};

  // Last states published to entities, compared in loop() to spot changes.
  bool published_muted_{false};
  bool published_off_hook_{false};
  bool published_ringing_{false};

  // Button states we send to the host.
  bool hook_button_{false};
  bool mute_button_{false};

  void send_telephony_report();
  // One mute button press/release pulse, scheduled rather than delay()ed.
  void send_mute_pulse_(bool telephony, bool consumer);
  void send_consumer_pulse_(uint8_t bit, const char *name);
  void publish_telephony_state_();

  // type() runs from loop() one keystroke at a time, so typing a long string no
  // longer blocks every other component for its whole duration.
  void type_loop_();
  std::string type_text_;
  size_t type_index_{0};
  uint32_t type_speed_ms_{50};
  uint32_t type_jitter_ms_{0};
  uint32_t type_next_time_{0};
  bool type_key_down_{false};
  
  // Telephony callbacks
  CallbackManager<void(bool)> mute_callbacks_;
  CallbackManager<void(bool)> off_hook_callbacks_;
  CallbackManager<void(bool)> ring_callbacks_;
  CallbackManager<void(bool)> hold_callbacks_;

#ifdef USE_HID_COMPOSITE_LAMP_ARRAY
  // LampArray state
  lamp_array_core::LampArrayCore lamp_array_;
  bool lamp_array_connected_{false};
  // Only diff frames when something actually listens for per-lamp updates.
  bool want_lamp_updates_{false};
  uint32_t last_lamp_frame_{0};
  std::vector<lamp_array_core::LampColor> lamp_incoming_;
  std::vector<lamp_array_core::LampColor> lamp_published_;

  CallbackManager<void(uint16_t, Color)> lamp_update_callbacks_;
  CallbackManager<void(bool)> autonomous_callbacks_;

  void setup_lamp_array_();
  void loop_lamp_array_();
#endif
};

#ifdef USE_HID_COMPOSITE_LAMP_ARRAY
// Folds the LampArray intensity channel into an RGB colour. With a single
// intensity level the channel is an on/off gate, which is what Windows expects
// from the Microsoft reference devices; with more levels it scales.
Color lamp_color_to_esphome(const lamp_array_core::LampColor &color, uint8_t intensity_levels);

class LampUpdateTrigger : public Trigger<uint16_t, Color> {
 public:
  explicit LampUpdateTrigger(HIDComposite *parent) {
    parent->add_on_lamp_update_callback([this](uint16_t lamp_id, Color color) { this->trigger(lamp_id, color); });
  }
};

class AutonomousModeTrigger : public Trigger<bool> {
 public:
  explicit AutonomousModeTrigger(HIDComposite *parent) {
    parent->add_on_autonomous_mode_callback([this](bool autonomous) { this->trigger(autonomous); });
  }
};
#endif

// ============ Mouse Action Templates ============

template<typename... Ts>
class MoveAction : public Action<Ts...>, public Parented<HIDComposite> {
 public:
  TEMPLATABLE_VALUE(int8_t, x)
  TEMPLATABLE_VALUE(int8_t, y)
  void play(Ts... x) override { this->parent_->move(this->x_.value(x...), this->y_.value(x...)); }
};

template<typename... Ts>
class ScrollAction : public Action<Ts...>, public Parented<HIDComposite> {
 public:
  TEMPLATABLE_VALUE(int8_t, vertical)
  TEMPLATABLE_VALUE(int8_t, horizontal)
  void play(Ts... x) override { this->parent_->scroll(this->vertical_.value(x...), this->horizontal_.value(x...)); }
};

template<typename... Ts>
class ClickAction : public Action<Ts...>, public Parented<HIDComposite> {
 public:
  TEMPLATABLE_VALUE(uint8_t, button)
  void play(Ts... x) override { this->parent_->click(static_cast<MouseButton>(this->button_.value(x...))); }
};

template<typename... Ts>
class MousePressAction : public Action<Ts...>, public Parented<HIDComposite> {
 public:
  TEMPLATABLE_VALUE(uint8_t, button)
  void play(Ts... x) override { this->parent_->mouse_press(static_cast<MouseButton>(this->button_.value(x...))); }
};

template<typename... Ts>
class MouseReleaseAction : public Action<Ts...>, public Parented<HIDComposite> {
 public:
  TEMPLATABLE_VALUE(uint8_t, button)
  void play(Ts... x) override { this->parent_->mouse_release(static_cast<MouseButton>(this->button_.value(x...))); }
};

template<typename... Ts>
class MouseReleaseAllAction : public Action<Ts...>, public Parented<HIDComposite> {
 public:
  void play(Ts... x) override { this->parent_->mouse_release_all(); }
};

// ============ Keyboard Action Templates ============

template<typename... Ts>
class KeyPressAction : public Action<Ts...>, public Parented<HIDComposite> {
 public:
  TEMPLATABLE_VALUE(std::string, key)
  void set_modifier(uint8_t mod) { this->modifier_ = mod; }
  void play(Ts... x) override { this->parent_->key_press(this->key_.value(x...), this->modifier_); }
 protected:
  uint8_t modifier_{0};
};

template<typename... Ts>
class KeyReleaseAction : public Action<Ts...>, public Parented<HIDComposite> {
 public:
  void play(Ts... x) override { this->parent_->key_release(); }
};

template<typename... Ts>
class KeyTapAction : public Action<Ts...>, public Parented<HIDComposite> {
 public:
  TEMPLATABLE_VALUE(std::string, key)
  void set_modifier(uint8_t mod) { this->modifier_ = mod; }
  void play(Ts... x) override { this->parent_->key_tap(this->key_.value(x...), this->modifier_); }
 protected:
  uint8_t modifier_{0};
};

template<typename... Ts>
class KeyReleaseAllAction : public Action<Ts...>, public Parented<HIDComposite> {
 public:
  void play(Ts... x) override { this->parent_->key_release_all(); }
};

template<typename... Ts>
class TypeAction : public Action<Ts...>, public Parented<HIDComposite> {
 public:
  TEMPLATABLE_VALUE(std::string, text)
  TEMPLATABLE_VALUE(uint32_t, speed)
  TEMPLATABLE_VALUE(uint32_t, jitter)
  void play(Ts... x) override {
    this->parent_->type(this->text_.value(x...), this->speed_.value(x...), this->jitter_.value(x...));
  }
};

// ============ Keep Awake Action Templates ============

template<typename... Ts>
class StartMouseKeepAwakeAction : public Action<Ts...>, public Parented<HIDComposite> {
 public:
  TEMPLATABLE_VALUE(uint32_t, interval)
  TEMPLATABLE_VALUE(uint32_t, jitter)
  void play(Ts... x) override {
    this->parent_->start_mouse_keep_awake(this->interval_.value(x...), this->jitter_.value(x...));
  }
};

template<typename... Ts>
class StopMouseKeepAwakeAction : public Action<Ts...>, public Parented<HIDComposite> {
 public:
  void play(Ts... x) override {
    this->parent_->stop_mouse_keep_awake();
  }
};

template<typename... Ts>
class StartKeyboardKeepAwakeAction : public Action<Ts...>, public Parented<HIDComposite> {
 public:
  TEMPLATABLE_VALUE(std::string, key)
  TEMPLATABLE_VALUE(uint32_t, interval)
  TEMPLATABLE_VALUE(uint32_t, jitter)
  void play(Ts... x) override {
    this->parent_->start_keyboard_keep_awake(this->key_.value(x...), this->interval_.value(x...), this->jitter_.value(x...));
  }
};

template<typename... Ts>
class StopKeyboardKeepAwakeAction : public Action<Ts...>, public Parented<HIDComposite> {
 public:
  void play(Ts... x) override {
    this->parent_->stop_keyboard_keep_awake();
  }
};

// ============ Telephony Action Templates ============

template<typename... Ts>
class MuteAction : public Action<Ts...>, public Parented<HIDComposite> {
 public:
  void play(Ts... x) override { this->parent_->mute(); }
};

template<typename... Ts>
class UnmuteAction : public Action<Ts...>, public Parented<HIDComposite> {
 public:
  void play(Ts... x) override { this->parent_->unmute(); }
};

template<typename... Ts>
class ToggleMuteAction : public Action<Ts...>, public Parented<HIDComposite> {
 public:
  void play(Ts... x) override { this->parent_->toggle_mute(); }
};

template<typename... Ts>
class MuteTelephonyAction : public Action<Ts...>, public Parented<HIDComposite> {
 public:
  void play(Ts... x) override { this->parent_->mute_telephony(); }
};

template<typename... Ts>
class MuteConsumerAction : public Action<Ts...>, public Parented<HIDComposite> {
 public:
  void play(Ts... x) override { this->parent_->mute_consumer(); }
};

template<typename... Ts>
class MuteTeamsAction : public Action<Ts...>, public Parented<HIDComposite> {
 public:
  void play(Ts... x) override { this->parent_->mute_teams(); }
};

template<typename... Ts>
class HookSwitchAction : public Action<Ts...>, public Parented<HIDComposite> {
 public:
  TEMPLATABLE_VALUE(bool, state)
  void play(Ts... x) override { this->parent_->hook_switch(this->state_.value(x...)); }
};

template<typename... Ts>
class AnswerCallAction : public Action<Ts...>, public Parented<HIDComposite> {
 public:
  void play(Ts... x) override { this->parent_->answer_call(); }
};

template<typename... Ts>
class HangUpAction : public Action<Ts...>, public Parented<HIDComposite> {
 public:
  void play(Ts... x) override { this->parent_->hang_up(); }
};

}  // namespace hid_composite
}  // namespace esphome
