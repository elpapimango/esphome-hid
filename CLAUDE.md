# CLAUDE.md - AI Assistant Guide for esphome-hid

This file provides context for AI assistants (Claude, Copilot, etc.) working with this repository.

## Project Overview

**esphome-hid** is a set of ESPHome external components that let ESP32-S3 (and S2/P4) microcontrollers act as USB HID devices: mouse, keyboard, telephony headset, and LampArray (Windows Dynamic Lighting). It uses TinyUSB for USB communication.

The components are mutually exclusive (each installs the TinyUSB driver and owns the USB descriptors), enforced with `CONFLICTS_WITH`. `hid_composite` is the combined device and is where most development happens.

## Repository Structure

```
esphome-hid/
├── components/
│   ├── hid_mouse/           # mouse only
│   ├── hid_keyboard/        # keyboard only
│   ├── hid_telephony/       # mute/call control only
│   ├── hid_lamp_array/      # LampArray only (host-driven lighting)
│   └── hid_composite/       # mouse + keyboard + telephony (+ optional LampArray)
│       ├── __init__.py      # ESPHome Python config, actions, effect registration
│       ├── hid_composite.h  # C++ header with class + action/trigger templates
│       ├── hid_composite.cpp # HID descriptor, TinyUSB callbacks, implementation
│       ├── lamp_array_core.h # LampArray protocol core (see "Duplication" below)
│       ├── lamp_array_light_effect.{h,cpp}  # addressable-light effect
│       ├── binary_sensor/   # connected / telephony state sub-platform
│       └── switch/          # keep-awake and mute sub-platform
├── examples/                # hand-run ESPHome configs, no automated tests
├── README.md                # User documentation
├── NOTICE.md                # Third-party (Microsoft, ESPHome) attributions
├── LICENSE                  # MIT License
└── CLAUDE.md                # This file
```

### Duplication is deliberate

ESPHome's `external_components: components: [x]` copies only directory `x`, so
shared code between components cannot live in a sibling directory. Keyboard and
mouse logic is therefore copy-pasted into `hid_composite`, and
`lamp_array_core.h` exists byte-identically in both `hid_lamp_array/` and
`hid_composite/`. `hid_lamp_array/lamp_array_core.h` is the source of truth:
edit it, then copy over the other. It uses its own `esphome::lamp_array_core`
namespace so the two copies never clash.

## Technical Stack

- **ESPHome**: Home automation firmware framework
- **ESP-IDF**: Espressif IoT Development Framework (required for USB support)
- **TinyUSB**: USB stack library (via ESPHome's built-in `tinyusb` component)
- **Target Hardware**: ESP32-S2, ESP32-S3, ESP32-P4 (chips with native USB OTG)

## Key Files Explained

### Report ID map (`hid_composite`)

| ID | Report |
|----|--------|
| 1 | Keyboard (input) |
| 2 | Mouse (input) |
| 3 | Telephony (input) |
| 4 | Telephony LEDs (output, host -> device) |
| 5 | Consumer Control (input) |
| 6-11 | LampArray (feature, both directions) — only with `lamp_array:` |

Enabling LampArray changes the descriptor, so those builds use PID `0x4007`
instead of `0x4004`. PIDs in use: `0x4002` mouse, `0x4003` keyboard, `0x4004`
composite, `0x4005` telephony, `0x4006` LampArray, `0x4007` composite+LampArray.

### `components/hid_mouse/__init__.py`
ESPHome Python configuration that:
- Declares the component with `CODEOWNERS` and `DEPENDENCIES`
- Defines the CONFIG_SCHEMA for YAML validation
- Registers automation actions (move, click, press, release, scroll)
- Sets ESP-IDF sdkconfig options for TinyUSB HID

### `components/hid_mouse/hid_mouse.h`
C++ header containing:
- `MouseButton` enum (LEFT=0x01, RIGHT=0x02, MIDDLE=0x04)
- `HIDMouse` class extending `esphome::Component`
- Template action classes (MoveAction, ClickAction, etc.)
- Conditional compilation with `HID_MOUSE_SUPPORTED` macro

### `components/hid_mouse/hid_mouse.cpp`
C++ implementation with:
- HID Report Descriptor (boot protocol compatible mouse)
- TinyUSB callbacks (`tud_hid_descriptor_report_cb`, etc.)
- Mouse action methods (move, click, press, release, scroll)
- Report sending via `tud_hid_report()`

## ESPHome Component Pattern

This follows ESPHome's external component pattern:
1. Python `__init__.py` handles YAML config parsing and code generation
2. C++ `.h/.cpp` files implement the actual component logic
3. Actions are registered in Python and implemented as template classes in C++

## Common Development Tasks

### Adding a new action
1. Add the action class in `hid_mouse.h` (template class extending `Action` and `Parented`)
2. Implement the `play()` method
3. Register the action in `__init__.py` with `@automation.register_action`
4. Define the schema and `to_code` async function

### Adding a new HID device type
1. Create a new component folder, e.g. `components/hid_foo/`
2. Follow the same pattern: `__init__.py`, `.h`, `.cpp`, plus sub-platform dirs
3. Define the HID report descriptor for the device type
4. Implement the TinyUSB callbacks
5. Pick an unused `idProduct` and add the component to every other component's
   `CONFLICTS_WITH`

### Direction of data

Most reports are **device -> host** (`tud_hid_report()`), sent from ESPHome
actions. Two paths run the other way:

- **Output reports** land in `tud_hid_set_report_cb` with
  `HID_REPORT_TYPE_OUTPUT` — telephony LED state (mute, off-hook, ring).
- **Feature reports** land in `tud_hid_get_report_cb` (host reads) and
  `tud_hid_set_report_cb` with `HID_REPORT_TYPE_FEATURE` (host writes) — the
  whole LampArray protocol. In both callbacks the buffer **excludes** the report
  ID, which arrives as a separate argument.

These callbacks run on the **TinyUSB task**, not the ESPHome loop. Never fire an
automation or touch an ESPHome entity from inside them: cache the state (atomics
or a mutex) and publish it from `loop()`. `lamp_array_core.h` and
`HIDComposite::publish_telephony_state_()` show the pattern.

### Never `delay()` in an action

HID buttons need a press/release gap, but `delay()` blocks the whole ESPHome
loop. Use `set_timeout()` for one-shot releases (`click`, `key_tap`, mute
pulses), and a `loop()`-driven state machine for long sequences (`type_loop_()`
sends one keystroke per tick).

### Testing compilation
```bash
# Windows: .venv/Scripts/python.exe -m esphome ...
python -m esphome config examples/lamp_array.yaml    # schema/codegen only, fast
python -m esphome compile examples/lamp_array.yaml   # full C++ build
```

## HID Report Structure

The mouse uses a 4-byte boot protocol report:
```cpp
typedef struct {
  uint8_t buttons;  // Bit 0: Left, Bit 1: Right, Bit 2: Middle
  int8_t x;         // Relative X movement (-127 to 127)
  int8_t y;         // Relative Y movement (-127 to 127)
  int8_t wheel;     // Scroll wheel (-127 to 127)
} hid_mouse_report_t;
```

## Dependencies

- ESPHome `esp32` component
- ESPHome `tinyusb` component (built-in, handles USB initialization)

## YAML Usage Example

```yaml
external_components:
  - source:
      type: git
      url: https://github.com/AntorFR/esphome-hid
      ref: main
    components: [hid_mouse]

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
4. **Boot Protocol**: Uses boot protocol for maximum compatibility
5. **Report ID**: `hid_mouse` alone uses report ID 0 (boot protocol, no report
   ID byte); the other components use explicit report IDs
6. **LampArray buffer size**: a `LampMultiUpdate` report is 51 bytes with its
   report ID, so LampArray builds need `-DCFG_TUD_HID_EP_BUFSIZE=64`. A
   `static_assert` catches a too-small buffer at build time
7. **No automated tests**: `examples/*.yaml` are run by hand. `esphome config
   examples/<file>.yaml` catches schema errors and `esphome compile` catches C++
   errors without hardware; behaviour needs a real ESP32-S3 on its native USB
   port
