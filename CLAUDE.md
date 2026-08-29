# CLAUDE.md - AI Assistant Guide for esphome-hid

This file provides context for AI assistants (Claude, Copilot, etc.) working with this repository.

## Project Overview

**esphome-hid** is a set of ESPHome external components that let ESP32-S2/S3/P4 microcontrollers act as
USB HID devices — mouse, keyboard, telephony (mute/call control), LampArray (Windows Dynamic Lighting),
or all of them combined. It uses TinyUSB for USB communication.

The components are mutually exclusive (each installs the TinyUSB driver and owns the USB descriptors),
enforced with `CONFLICTS_WITH`. `hid_composite` is the combined device and is where most development
happens.

## Repository Structure

```
esphome-hid/
├── components/
│   ├── hid_mouse/            # Mouse only (4-byte boot-protocol report, report ID 0)
│   │   ├── binary_sensor/    # `connected` status sensor
│   │   └── switch/           # Keep-awake switch
│   ├── hid_keyboard/         # Keyboard only
│   │   ├── binary_sensor/
│   │   └── switch/
│   ├── hid_telephony/        # Mute/call control only, with PC -> device state sync
│   │   ├── binary_sensor/    # `connected`, `muted`, `in_call`, `ringing`
│   │   └── switch/           # Mute switch (bidirectional)
│   ├── hid_lamp_array/       # LampArray only (host-driven lighting)
│   │   ├── binary_sensor/    # `connected`, `autonomous`
│   │   └── lamp_array_core.h # LampArray protocol core (see "Duplication" below)
│   └── hid_composite/        # Mouse + keyboard + telephony (+ optional LampArray)
│       ├── __init__.py       # ESPHome Python config, actions, effect registration
│       ├── hid_composite.h   # C++ header with class + action/trigger templates
│       ├── hid_composite.cpp # HID descriptor, TinyUSB callbacks, implementation
│       ├── lamp_array_core.h # Byte-identical copy of hid_lamp_array's (see below)
│       ├── lamp_array_light_effect.{h,cpp}  # addressable-light effect
│       ├── binary_sensor/    # connected / telephony / lamp state sub-platform
│       └── switch/           # keep-awake and mute sub-platform
├── examples/                 # Example + smoke-test ESPHome configs, hand-run, no automated tests
├── .github/workflows/        # CI: esphome config/compile over examples/
├── README.md                 # User documentation
├── NOTICE.md                 # Third-party (Microsoft, ESPHome) attributions
├── LICENSE                   # MIT License
└── CLAUDE.md                 # This file
```

> **Mutually exclusive**: exactly one of these five components goes in a given device's YAML — each
> owns the USB descriptor (`CONFLICTS_WITH` in every `__init__.py` enforces this at config time).

### Duplication is deliberate

ESPHome's `external_components: components: [x]` copies only directory `x`, so shared code between
components cannot live in a sibling directory. Keyboard and mouse logic is therefore copy-pasted into
`hid_composite`, and `lamp_array_core.h` exists byte-identically in both `hid_lamp_array/` and
`hid_composite/`. `hid_lamp_array/lamp_array_core.h` is the source of truth: edit it, then copy over the
other. It uses its own `esphome::lamp_array_core` namespace so the two copies never clash.

## Technical Stack

- **ESPHome**: Home automation firmware framework
- **ESP-IDF**: Espressif IoT Development Framework (required for USB support)
- **TinyUSB**: USB stack library (via ESPHome's built-in `tinyusb` component)
- **Target Hardware**: ESP32-S2, ESP32-S3, ESP32-P4 (chips with native USB OTG). Guarded per component via
  `USE_ESP32_VARIANT_ESP32S2` / `_ESP32S3` / `_ESP32P4` in each `.h`.

## Key Files Explained (pattern shared by all five components)

### Report ID map (`hid_composite`)

| ID | Report |
|----|--------|
| 1 | Keyboard (input) |
| 2 | Mouse (input) |
| 3 | Telephony (input) |
| 4 | Telephony LEDs (output, host -> device) |
| 5 | Consumer Control (input) |
| 6-11 | LampArray (feature, both directions) — only with `lamp_array:` |

Enabling LampArray changes the descriptor, so those builds use PID `0x4007` instead of `0x4004`. PIDs in
use: `0x4002` mouse, `0x4003` keyboard, `0x4004` composite, `0x4005` telephony, `0x4006` LampArray,
`0x4007` composite+LampArray.

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

### Direction of data

Most reports are **device -> host** (`tud_hid_report()`), sent from ESPHome actions. Two paths run the
other way:

- **Output reports** land in `tud_hid_set_report_cb` with `HID_REPORT_TYPE_OUTPUT` — telephony LED state
  (mute, off-hook, ring).
- **Feature reports** land in `tud_hid_get_report_cb` (host reads) and `tud_hid_set_report_cb` with
  `HID_REPORT_TYPE_FEATURE` (host writes) — the whole LampArray protocol. In both callbacks the buffer
  **excludes** the report ID, which arrives as a separate argument.

These callbacks run on the **TinyUSB task**, not the ESPHome loop. Never fire an automation or touch an
ESPHome entity from inside them: cache the state (atomics or a mutex) and publish it from `loop()`.
`lamp_array_core.h` and `HIDComposite::publish_telephony_state_()` show the pattern.

### Never `delay()` in an action

HID buttons need a press/release gap, but `delay()` blocks the whole ESPHome loop. Use `set_timeout()`
for one-shot releases (`click`, `key_tap`, mute pulses), and a `loop()`-driven state machine for long
sequences (`type_loop_()` sends one keystroke per tick).

### Testing
```bash
source .venv/bin/activate

# Schema-only validation (fast, no compiler needed)
esphome config examples/test_composite_telephony.yaml
esphome config examples/lamp_array.yaml

# Full build (slow, first run downloads the ESP-IDF toolchain) — this is the
# only thing that catches signature mismatches in the unsupported-chip stub
# block, since none of the example boards take that branch
esphome compile examples/test_composite_telephony.yaml
esphome compile examples/lamp_array.yaml
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
  system mute/volume (ID 5), a Telephony Devices collection (input ID 3, output/LED ID 4), and
  optionally LampArray (IDs 6-11, feature reports both directions).
- **`hid_telephony`**: Telephony Devices page (ID 1, Poly BT700-style: Hook Switch + relative Phone
  Mute as input, five LED bits as output) plus a Consumer Control page (ID 2) and a minimal
  input-only keyboard page (ID 3, used only by `mute_teams` to send Ctrl+Shift+M).
- Only the Telephony page's report carries an *output* (LED) item — that's the only path that gives
  PC → device mute-state sync. The Consumer/keyboard mute paths are one-way and exist as fallbacks for
  hosts that don't recognize the Telephony page.
- **`hid_lamp_array`**: Feature reports only (host -> device and device -> host), no input/output items —
  see "Direction of data" above.

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
    components: [hid_mouse]  # or hid_keyboard / hid_telephony / hid_lamp_array / hid_composite

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
4. **One HID component per device**: `hid_mouse`/`hid_keyboard`/`hid_telephony`/`hid_lamp_array`/
   `hid_composite` conflict with each other — each owns the whole USB descriptor
5. **`hid_mouse` uses boot protocol** (report ID 0); the other four use report-ID'd descriptors and are
   not boot-protocol compatible
6. **Stub block parity**: when adding a method to a component's header, add it to both the real
   implementation *and* the `#else` (unsupported-chip) stub block in the same `.cpp`, with an identical
   signature — this is a compile error only on an unbuilt code path, so nothing short of grepping or an
   actual non-S2/S3/P4 build will catch a mismatch
7. **LampArray buffer size**: a `LampMultiUpdate` report is 51 bytes with its report ID, so LampArray
   builds need `-DCFG_TUD_HID_EP_BUFSIZE=64`. A `static_assert` catches a too-small buffer at build time
8. **No automated tests**: `examples/*.yaml` are run by hand. `esphome config examples/<file>.yaml`
   catches schema errors and `esphome compile` catches C++ errors without hardware; behaviour needs a
   real ESP32-S2/S3/P4 on its native USB port
