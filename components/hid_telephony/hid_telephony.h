#pragma once

#include "esphome/core/component.h"
#include "esphome/core/automation.h"

#ifdef USE_ESP32

#include <atomic>
#include <functional>

// Chips with native USB OTG (ESP32-S2, S3, P4). Ask the SoC caps header rather
// than listing targets, so every HID component agrees on the same test.
#include <soc/soc_caps.h>
#if SOC_USB_OTG_SUPPORTED
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

  // Mute control. The mute button is a toggle on the wire, so these drive the
  // PC towards the requested state using the mute state it last reported.
  void mute();        // Mute if not already muted
  void unmute();      // Unmute if currently muted
  void toggle_mute();  // Unconditional toggle press
  void set_mute(bool state);

  // Mute control séparé pour test
  void mute_telephony();   // Envoie uniquement le rapport Telephony (0x0B)
  void mute_consumer();    // Envoie uniquement le rapport Consumer (0x0C)

  // Call control
  void hook_switch();  // Toggle off-hook/on-hook
  void answer();       // Go off-hook (answer call)
  void hang_up();      // Go on-hook (end call)

  // State getters. Written from the TinyUSB task, read from the ESPHome loop.
  bool is_muted() const { return this->muted_.load(); }
  bool is_off_hook() const { return this->off_hook_.load(); }
  bool is_ringing() const { return this->ringing_.load(); }
  // True once the host has sent at least one LED report, i.e. the states above
  // reflect the PC rather than our defaults.
  bool host_state_known() const { return this->host_state_known_.load(); }
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
  // One mute button press/release pulse, scheduled rather than delay()ed.
  void send_mute_pulse_(bool telephony, bool consumer);

  bool initialized_{false};

  // Button states (what we send to host)
  bool mute_button_{false};
  bool hook_button_{false};

  // LED states (what the host tells us). Written on the TinyUSB task, so these
  // are atomic and the matching automations run from loop() instead.
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

  // Callbacks
  CallbackManager<void(bool)> mute_callbacks_;
  CallbackManager<void(bool)> off_hook_callbacks_;
  CallbackManager<void(bool)> ring_callbacks_;
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
