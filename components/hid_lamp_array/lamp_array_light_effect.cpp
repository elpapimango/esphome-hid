#include "lamp_array_light_effect.h"

#if defined(USE_LIGHT) && defined(USE_ESP32)

#include "esphome/core/log.h"

namespace esphome {
namespace hid_lamp_array {

static const char *const TAG = "hid_lamp_array.effect";

void LampArrayLightEffect::start() {
  AddressableLightEffect::start();

  if (this->lamp_array_ == nullptr)
    return;

  this->buffer_.assign(this->lamp_array_->lamp_count(), LampColor{0, 0, 0, 0});
  // Force the first apply() to paint rather than trusting stale counters.
  this->last_frame_ = this->lamp_array_->core().frame() - 1;
  this->painted_autonomous_ = false;

  ESP_LOGD(TAG, "Rendering %u lamps from offset %u", this->lamp_array_->lamp_count(), this->offset_);
}

void LampArrayLightEffect::apply(light::AddressableLight &it, const Color &current_color) {
  if (this->lamp_array_ == nullptr)
    return;

  if (this->lamp_array_->is_autonomous()) {
    // Host is not driving us: hand the strip back to the light's own colour,
    // repainting only when that colour actually changes.
    // Color::operator== is non-const, so the member has to be the left operand.
    if (this->painted_autonomous_ && this->last_autonomous_color_ == current_color)
      return;
    it.all() = current_color;
    it.schedule_show();
    this->painted_autonomous_ = true;
    this->last_autonomous_color_ = current_color;
    return;
  }

  this->painted_autonomous_ = false;

  const uint32_t frame = this->lamp_array_->core().frame();
  if (frame == this->last_frame_)
    return;
  this->last_frame_ = frame;

  this->lamp_array_->core().snapshot(this->buffer_.data());

  const uint8_t levels = this->lamp_array_->core().intensity_levels();
  const int32_t leds = it.size();
  for (int32_t i = 0; i < leds; i++) {
    const uint32_t lamp_id = this->offset_ + i;
    if (lamp_id >= this->buffer_.size())
      break;
    it[i].set(lamp_color_to_esphome(this->buffer_[lamp_id], levels));
  }
  it.schedule_show();
}

}  // namespace hid_lamp_array
}  // namespace esphome

#endif  // USE_LIGHT && USE_ESP32
