#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"

#ifdef USE_ESP32

#include <functional>

// Check for ESP32-S2, ESP32-S3, or ESP32-P4 (chips with USB OTG)
#if defined(USE_ESP32_VARIANT_ESP32S2) || defined(USE_ESP32_VARIANT_ESP32S3) || defined(USE_ESP32_VARIANT_ESP32P4)
#define HID_TELEPHONY_SUPPORTED
#endif

namespace esphome {
namespace hid_telephony {

class HIDTelephony : public Component {
 public:
  void setup() override;
  void loop() override;
  void dump_config() override;
  float get_setup_priority() const override { return setup_priority::AFTER_WIFI; }

  // Mute control - envoie les deux rapports (Telephony + Consumer)
  void mute();
  void unmute();
  void toggle_mute();
  
  // Separate mute paths, useful for testing which one a given host recognizes
  void mute_telephony();   // Sends only the Telephony report (page 0x0B)
  void mute_consumer();    // Sends only the Consumer report (page 0x0C)
  void mute_teams();       // Sends Ctrl+Shift+M (Teams keyboard shortcut)
  
  // Volume control (Consumer Control)
  void volume_up();
  void volume_down();
  
  // Call control
  void hook_switch();  // Toggle off-hook/on-hook
  void answer();       // Go off-hook (answer call)
  void hang_up();      // Go on-hook (end call)
  
  // State getters
  bool is_muted() const { return this->muted_; }
  bool is_off_hook() const { return this->off_hook_; }
  bool is_ringing() const { return this->ringing_; }
  bool is_connected();
  bool is_ready();
  
  // Callbacks for state changes
  void add_on_mute_callback(std::function<void(bool)> &&callback) {
    this->mute_callbacks_.add(std::move(callback));
  }
  void add_on_off_hook_callback(std::function<void(bool)> &&callback) {
    this->off_hook_callbacks_.add(std::move(callback));
  }
  void add_on_ring_callback(std::function<void(bool)> &&callback) {
    this->ring_callbacks_.add(std::move(callback));
  }

  // Called from TinyUSB callback
  void process_host_report(uint8_t const *buffer, uint16_t bufsize);

 protected:
  void send_report_();
  void send_consumer_mute_();
  void send_keyboard_report_(uint8_t modifier, uint8_t keycode);
  // Updates the cached mute state and notifies listeners when it actually changes.
  void set_muted_(bool muted);
  
  bool initialized_{false};
  
  // Button states (what we send to host)
  bool mute_button_{false};
  bool hook_button_{false};
  
  // LED states (what host tells us)
  bool muted_{false};
  bool off_hook_{false};
  bool ringing_{false};
  bool hold_{false};
  
  // Callbacks
  CallbackManager<void(bool)> mute_callbacks_;
  CallbackManager<void(bool)> off_hook_callbacks_;
  CallbackManager<void(bool)> ring_callbacks_;
  
  friend void telephony_set_report_callback(uint8_t const *buffer, uint16_t bufsize);
};

// Global instance for callbacks
extern HIDTelephony *g_hid_telephony_instance;

// Action Templates
template<typename... Ts>
class MuteAction : public Action<Ts...>, public Parented<HIDTelephony> {
 public:
  void play(Ts... x) override { this->parent_->mute(); }
};

template<typename... Ts>
class UnmuteAction : public Action<Ts...>, public Parented<HIDTelephony> {
 public:
  void play(Ts... x) override { this->parent_->unmute(); }
};

template<typename... Ts>
class ToggleMuteAction : public Action<Ts...>, public Parented<HIDTelephony> {
 public:
  void play(Ts... x) override { this->parent_->toggle_mute(); }
};

template<typename... Ts>
class MuteTelephonyAction : public Action<Ts...>, public Parented<HIDTelephony> {
 public:
  void play(Ts... x) override { this->parent_->mute_telephony(); }
};

template<typename... Ts>
class MuteConsumerAction : public Action<Ts...>, public Parented<HIDTelephony> {
 public:
  void play(Ts... x) override { this->parent_->mute_consumer(); }
};

template<typename... Ts>
class MuteTeamsAction : public Action<Ts...>, public Parented<HIDTelephony> {
 public:
  void play(Ts... x) override { this->parent_->mute_teams(); }
};

template<typename... Ts>
class VolumeUpAction : public Action<Ts...>, public Parented<HIDTelephony> {
 public:
  void play(Ts... x) override { this->parent_->volume_up(); }
};

template<typename... Ts>
class VolumeDownAction : public Action<Ts...>, public Parented<HIDTelephony> {
 public:
  void play(Ts... x) override { this->parent_->volume_down(); }
};

template<typename... Ts>
class HookSwitchAction : public Action<Ts...>, public Parented<HIDTelephony> {
 public:
  void play(Ts... x) override { this->parent_->hook_switch(); }
};

template<typename... Ts>
class AnswerAction : public Action<Ts...>, public Parented<HIDTelephony> {
 public:
  void play(Ts... x) override { this->parent_->answer(); }
};

template<typename... Ts>
class HangUpAction : public Action<Ts...>, public Parented<HIDTelephony> {
 public:
  void play(Ts... x) override { this->parent_->hang_up(); }
};

}  // namespace hid_telephony
}  // namespace esphome

#endif  // USE_ESP32
