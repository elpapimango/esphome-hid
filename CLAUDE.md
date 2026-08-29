# CLAUDE.md - AI Assistant Guide for esphome-hid

This file provides context for AI assistants (Claude, Copilot, etc.) working with this repository.

## Project Overview

**esphome-hid** is a set of ESPHome external components that let ESP32-S2/S3/P4 microcontrollers act as
USB HID devices — mouse, keyboard, telephony (mute/call control), or all three combined. It uses TinyUSB
for USB communication.

## Repository Structure

```
esphome-hid/
├── components/
│   ├── hid_mouse/         # Mouse only (4-byte boot-protocol report, report ID 0)
│   │   ├── binary_sensor/ # `connected` status sensor
│   │   └── switch/        # Keep-awake switch
│   ├── hid_keyboard/       # Keyboard only
│   │   ├── binary_sensor/
│   │   └── switch/
│   ├── hid_telephony/      # Mute/call control only, with PC -> device state sync
│   │   ├── binary_sensor/  # `connected`, `muted`, `in_call`, `ringing`
│   │   └── switch/         # Mute switch (bidirectional)
│   └── hid_composite/      # Mouse + keyboard + telephony combined
│       ├── binary_sensor/
│       └── switch/
├── examples/                # Example + smoke-test ESPHome configs
├── .github/workflows/       # CI: esphome config/compile over examples/
├── README.md                 # User documentation
├── LICENSE                   # MIT License
└── CLAUDE.md                 # This file
```

> **Mutually exclusive**: exactly one of these four components goes in a given device's YAML — each
> owns the USB descriptor (`CONFLICTS_WITH` in every `__init__.py` enforces this at config time).

## Technical Stack

- **ESPHome**: Home automation firmware framework
- **ESP-IDF**: Espressif IoT Development Framework (required for USB support)
- **TinyUSB**: USB stack library (via ESPHome's built-in `tinyusb` component)
- **Target Hardware**: ESP32-S2, ESP32-S3, ESP32-P4 (chips with native USB OTG). Guarded per component via
  `USE_ESP32_VARIANT_ESP32S2` / `_ESP32S3` / `_ESP32P4` in each `.h`.

## Key Files Explained (pattern shared by all four components)

### `components/<name>/__init__.py`
ESPHome Python configuration that:
- Declares the component with `CODEOWNERS`, `DEPENDENCIES`, `CONFLICTS_WITH`
- Defines the `CONFIG_SCHEMA` for YAML validation
- Registers automation actions (e.g. `move`, `click`, `mute`, `type`)
- Sets ESP-IDF sdkconfig options for TinyUSB HID

### `components/<name>/<name>.h`
C++ header containing the `Component` subclass, its action-template classes (`Action<Ts...>` +
`Parented<T>`), and the `HID_<NAME>_SUPPORTED` guard macro.

### `components/<name>/<name>.cpp`
C++ implementation: the HID report descriptor, TinyUSB callbacks (`tud_hid_descriptor_report_cb`,
`tud_hid_set_report_cb`, ...), and the action methods that call `tud_hid_report()`. Ends with an
`#else` stub block (empty method bodies) for chips without USB OTG — **its signatures must exactly
match the header**; `esphome config` does not compile C++ and will not catch a mismatch there.

## ESPHome Component Pattern

1. Python `__init__.py` handles YAML config parsing and code generation
2. C++ `.h/.cpp` files implement the actual component logic
3. Actions are registered in Python and implemented as template classes in C++
4. `binary_sensor/` and `switch/` subfolders are ESPHome platform components that take
   `cv.use_id(HID<Name>)` and call back into the parent component

## Common Development Tasks

### Adding a new action
1. Add the action class in `<name>.h` (template class extending `Action` and `Parented`)
2. Implement the `play()` method
3. Register the action in `__init__.py` with `@automation.register_action`
4. Define the schema and `to_code` async function
5. Add the matching method (and stub) in `<name>.cpp`, and a row in `README.md`

### Adding a new HID device type
1. Create a new component folder: `components/hid_<name>/`
2. Follow the same pattern: `__init__.py`, `.h`, `.cpp`, `binary_sensor/`, `switch/`
3. Define the HID report descriptor for the device type; pick unused report IDs
4. Implement TinyUSB callbacks
5. Add it to every other component's `CONFLICTS_WITH`, and vice versa

### Testing
```bash
source .venv/bin/activate

# Schema-only validation (fast, no compiler needed)
esphome config examples/test_composite_telephony.yaml

# Full build (slow, first run downloads the ESP-IDF toolchain) — this is the
# only thing that catches signature mismatches in the unsupported-chip stub
# block, since none of the example boards take that branch
esphome compile examples/test_composite_telephony.yaml
```
CI (`.github/workflows/validate.yml`) runs both across all local-path examples on every push/PR.

## HID Report Structures

- **`hid_mouse` (standalone)**: 4-byte boot-protocol report, report ID 0 (no report ID byte):
  ```cpp
  typedef struct {
    uint8_t buttons;  // Bit 0: Left, Bit 1: Right, Bit 2: Middle
    int8_t x;         // Relative X movement (-127 to 127)
    int8_t y;         // Relative Y movement (-127 to 127)
    int8_t wheel;     // Scroll wheel (-127 to 127)
  } hid_mouse_report_t;
  ```
- **`hid_composite`**: multiple report-ID'd collections in one descriptor — keyboard (ID 1), mouse
  (ID 2, 5 bytes: buttons, x, y, vertical wheel, horizontal wheel), Consumer Control for
  system mute/volume (ID 5), and a Telephony Devices collection (input ID 3, output/LED ID 4).
- **`hid_telephony`**: Telephony Devices page (ID 1, Poly BT700-style: Hook Switch + relative Phone
  Mute as input, five LED bits as output) plus a Consumer Control page (ID 2) and a minimal
  input-only keyboard page (ID 3, used only by `mute_teams` to send Ctrl+Shift+M).
- Only the Telephony page's report carries an *output* (LED) item — that's the only path that gives
  PC → device mute-state sync. The Consumer/keyboard mute paths are one-way and exist as fallbacks for
  hosts that don't recognize the Telephony page.

## Dependencies

- ESPHome `esp32` component
- ESPHome `tinyusb` component (built-in, handles USB initialization)

## YAML Usage Example

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/elpapimango/esphome-hid
      ref: main
    components: [hid_mouse]  # or hid_keyboard / hid_telephony / hid_composite

tinyusb:
  usb_product_str: "My HID Mouse"

hid_mouse:
  id: my_mouse

# Use in automations
binary_sensor:
  - platform: gpio
    pin: GPIO0
    on_press:
      - hid_mouse.click:
          id: my_mouse
          button: LEFT
```

## Gotchas and Notes

1. **USB OTG Required**: Only ESP32-S2, S3, and P4 have native USB OTG
2. **ESP-IDF Framework**: Must use `esp-idf` framework, not Arduino
3. **TinyUSB Dependency**: The `tinyusb` component must be included in the config
4. **One HID component per device**: `hid_mouse`/`hid_keyboard`/`hid_telephony`/`hid_composite` conflict
   with each other — each owns the whole USB descriptor
5. **`hid_mouse` uses boot protocol** (report ID 0); the other three use report-ID'd descriptors and are
   not boot-protocol compatible
6. **Stub block parity**: when adding a method to a component's header, add it to both the real
   implementation *and* the `#else` (unsupported-chip) stub block in the same `.cpp`, with an identical
   signature — this is a compile error only on an unbuilt code path, so nothing short of grepping or an
   actual non-S2/S3/P4 build will catch a mismatch
