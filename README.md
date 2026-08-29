# ESPHome HID Components

ESPHome external components to simulate USB HID devices (mouse, keyboard, telephony, LampArray) on ESP32-S3.

Based on [AntorFR/esphome-hid](https://github.com/AntorFR/esphome-hid); this repo has since diverged
and is maintained independently.

## Components

| Component | Description |
|-----------|-------------|
| `hid_mouse` | USB HID Mouse only |
| `hid_keyboard` | USB HID Keyboard only |
| `hid_composite` | USB HID Mouse + Keyboard + Telephony (+ optional LampArray) combined |
| `hid_telephony` | USB HID Telephony only (mute/call control) |
| `hid_lamp_array` | USB HID LampArray only (Windows Dynamic Lighting) |

> **Note**: These components are mutually exclusive. Use only ONE of them in your configuration.

## Features

### Mouse (`hid_mouse` or `hid_composite`)
- Relative cursor movements (X, Y)
- Left, right, and middle buttons
- Vertical scroll wheel; horizontal scroll (AC Pan) on `hid_composite` only
- Keep awake (prevents PC sleep)

### Keyboard (`hid_keyboard` or `hid_composite`)
- Key press/release/tap
- All standard keys (A-Z, 0-9, F1-F12, arrows, etc.)
- Modifier keys (Ctrl, Shift, Alt, GUI/Win/Cmd)
- Type entire text strings with realistic speed/jitter
- Keyboard shortcuts (Ctrl+C, Alt+Tab, etc.)
- Keep awake (prevents PC sleep)

### Telephony (`hid_telephony` or `hid_composite`)
- Mute/unmute control
- Answer/hang up calls
- **Bidirectional sync**: PC mute state is reflected in Home Assistant
- Binary sensors for call state (in_call, ringing)
- Switch component for mute control

### LampArray (`hid_lamp_array` or `hid_composite`)
- Windows 11 Dynamic Lighting support (any app using `Windows.Devices.Lights`)
- Host pushes per-lamp RGB + intensity; the device renders it
- Renders straight onto any ESPHome addressable light via the `lamp_array` effect
- Lamp geometry declared in millimetres, auto-gridded or listed per lamp
- Autonomous-mode signal so device-side effects take over when the host lets go
- `on_lamp_update` / `on_autonomous_mode` automations for non-LED targets

### Connection Status
All components provide:
- `is_connected()` - PC connected via USB
- Binary sensor for connection status

## Requirements

- **ESP32-S3** with native USB OTG port
- **ESP-IDF framework** (default since ESPHome 2026.1.0)
- **ESPHome 2025.8.0+** recommended (2026.1.0+ for best experience)
- Board with **two USB ports** recommended:
  - **UART/CH340 port**: for flashing and serial logs
  - **USB native/JTAG port**: for HID devices

> **Important**: The HID device will only work on the **USB native/JTAG port**, not the UART/CH340 port.

## Installation

```yaml
# For mouse only
external_components:
  - source:
      type: git
      url: https://github.com/elpapimango/esphome-hid
      ref: main
    components: [hid_mouse]

# For keyboard only
external_components:
  - source:
      type: git
      url: https://github.com/elpapimango/esphome-hid
      ref: main
    components: [hid_keyboard]

# For both mouse and keyboard
external_components:
  - source:
      type: git
      url: https://github.com/elpapimango/esphome-hid
      ref: main
    components: [hid_composite]
```

## Configuration

### Required ESP-IDF Settings

```yaml
esphome:
  name: esp32-hid
  build_flags:
    - -DCFG_TUD_HID=1
    # Only needed for LampArray: a multi-update report is 51 bytes with its
    # report ID and has to fit TinyUSB's HID buffer.
    - -DCFG_TUD_HID_EP_BUFSIZE=64

esp32:
  board: esp32-s3-devkitc-1
  framework:
    type: esp-idf  # Default since ESPHome 2026.1.0
    sdkconfig_options:
      CONFIG_USJ_ENABLE_USB_SERIAL_JTAG: "n"
      CONFIG_TINYUSB_HID_COUNT: "1"
      CONFIG_ESP_CONSOLE_UART_DEFAULT: "y"
    components:
      - espressif/esp_tinyusb~2.0.0
```

## Mouse Actions

```yaml
hid_mouse:
  id: my_mouse
```

| Action | Description |
|--------|-------------|
| `hid_mouse.move` | Move cursor (x, y: -127 to 127) |
| `hid_mouse.click` | Click button (LEFT, RIGHT, MIDDLE) |
| `hid_mouse.press` | Press button |
| `hid_mouse.release` | Release button |
| `hid_mouse.release_all` | Release all buttons |
| `hid_mouse.scroll` | Scroll wheel (`amount`: -127 to 127). Vertical only; use `hid_composite.scroll` for horizontal |

## Keyboard Actions

```yaml
hid_keyboard:
  id: my_keyboard
  layout: AZERTY_FR  # Optional: QWERTY_US (default), AZERTY_FR, QWERTZ_DE
```

| Action | Description |
|--------|-------------|
| `hid_keyboard.press` | Press key |
| `hid_keyboard.release` | Release all keys |
| `hid_keyboard.tap` | Press and release key |
| `hid_keyboard.type` | Type text string |

### Keyboard Layouts

The `layout` option configures the keyboard mapping for the `type` action. This must match the keyboard layout configured on the target PC:

| Layout | Description |
|--------|-------------|
| `QWERTY_US` | US English (default) |
| `AZERTY_FR` | French AZERTY |
| `QWERTZ_DE` | German QWERTZ |

> **Note**: The layout affects `type`, and also `press`/`tap` when `key` is a
> single character (those go through the same character mapping). Multi-character
> key names (`ENTER`, `F15`, `TAB`, ...) are raw scancodes and are layout-independent.

### Special Keys
ENTER, ESC, BACKSPACE, TAB, SPACE, DELETE, INSERT, HOME, END, PAGEUP, PAGEDOWN, UP, DOWN, LEFT, RIGHT, F1-F24

> F13-F24 have no keycaps on normal keyboards, which makes F15 the usual
> keep-awake key: the PC registers activity but nothing visible happens.

### Modifiers
NONE, CTRL, SHIFT, ALT, GUI (WIN/CMD), CTRL_SHIFT, CTRL_ALT, CTRL_GUI, etc.

### Examples

```yaml
# Type text
- hid_keyboard.type:
    text: "Hello World!"

# Ctrl+C
- hid_keyboard.tap:
    key: "c"
    modifiers: CTRL

# Alt+Tab
- hid_keyboard.tap:
    key: "TAB"
    modifiers: ALT
```

## Composite Actions

```yaml
hid_composite:
  id: my_hid
  layout: AZERTY_FR  # Optional: QWERTY_US (default), AZERTY_FR, QWERTZ_DE
```

Mouse: `hid_composite.move`, `hid_composite.click`, `hid_composite.mouse_press`, `hid_composite.mouse_release`, `hid_composite.scroll`

Keyboard: `hid_composite.key_press`, `hid_composite.key_tap`, `hid_composite.key_release`, `hid_composite.type`

Telephony: `hid_composite.mute`, `hid_composite.unmute`, `hid_composite.toggle_mute`, `hid_composite.mute_telephony`, `hid_composite.mute_consumer`, `hid_composite.mute_teams`, `hid_composite.hook_switch`, `hid_composite.answer_call`, `hid_composite.hang_up`, `hid_composite.volume_up`, `hid_composite.volume_down`

Keep Awake: `hid_composite.start_mouse_keep_awake`, `hid_composite.stop_mouse_keep_awake`, `hid_composite.start_keyboard_keep_awake`, `hid_composite.stop_keyboard_keep_awake`

## Telephony Actions

```yaml
hid_telephony:
  id: my_telephony
```

| Action | Description |
|--------|-------------|
| `hid_telephony.mute` | Mute via the Telephony page (recommended default — supports bidirectional LED sync) |
| `hid_telephony.unmute` | Unmute via the Telephony page |
| `hid_telephony.toggle_mute` | Toggle mute via the Telephony page |
| `hid_telephony.mute_telephony` | Toggle mute, Telephony page only (page 0x0B) — diagnostic |
| `hid_telephony.mute_consumer` | Toggle mute, Consumer page only (page 0x0C) — diagnostic |
| `hid_telephony.mute_teams` | Send Ctrl+Shift+M — Teams' own mute shortcut, for hosts that don't recognize the device as a call-control headset |
| `hid_telephony.volume_up` | Volume up (Consumer page) |
| `hid_telephony.volume_down` | Volume down (Consumer page) |
| `hid_telephony.hook_switch` | Toggle off-hook/on-hook |
| `hid_telephony.answer` | Answer incoming call |
| `hid_telephony.hang_up` | End current call |

> **Note**: `mute`/`unmute`/`toggle_mute` use the Telephony page report, the only one with an LED report the host can send back — that's what makes bidirectional sync (mute state reflected in Home Assistant) possible. `mute_telephony`/`mute_consumer`/`mute_teams` exist because host recognition of the Telephony page varies; use them to find what your OS/app responds to, then prefer `mute` for anything wired to a switch or automation.

## LampArray

LampArray is the one HID feature that runs host -> device: Windows sends colours,
the ESP renders them. Everything is carried on Feature reports, so no input
actions exist for it.

```yaml
hid_lamp_array:
  id: my_lamps
  lamp_count: 24            # 1-1024
  kind: PERIPHERAL          # KEYBOARD, MOUSE, GAME_CONTROLLER, PERIPHERAL, SCENE,
                            # NOTIFICATION, CHASSIS, WEARABLE, FURNITURE, ART
  width: 460mm              # bounding box; the host uses it for spatial effects
  height: 10mm
  depth: 10mm
  rows: 1                   # lamps are auto-gridded across the bounding box
  purposes: [ACCENT]        # CONTROL, ACCENT, BRANDING, STATUS, ILLUMINATION, PRESENTATION
  min_update_interval: 33ms
  update_latency: 33ms
  intensity_levels: 1       # 1 = intensity is an on/off gate (what Windows expects)
  lamps:                    # optional; overrides the grid, must match lamp_count
    - {x: 0mm, y: 0mm, purposes: [STATUS]}
    - {x: 19mm, y: 0mm}
  on_lamp_update:
    - lambda: 'ESP_LOGD("lamps", "%u -> %02X%02X%02X", lamp_id, color.r, color.g, color.b);'
  on_autonomous_mode:
    - lambda: 'ESP_LOGD("lamps", "host released control: %d", autonomous);'
```

Under `hid_composite` the same block nests one level down and takes report IDs
6-11:

```yaml
hid_composite:
  id: my_hid
  lamp_array:
    lamp_count: 12
    kind: KEYBOARD
    rows: 3
```

> **Note**: enabling LampArray on `hid_composite` changes the report descriptor,
> so the device enumerates under PID `0x4007` rather than `0x4004`. Windows caches
> descriptors per VID/PID, so this avoids a stale cache.

### Rendering on an addressable light

```yaml
light:
  - platform: esp32_rmt_led_strip
    id: strip
    num_leds: 24
    # ...
    effects:
      - lamp_array:
          lamp_array_id: my_lamps   # hid_composite_id: my_hid, for the composite
          offset: 0                 # first lamp id shown on LED 0
```

Select the effect and Windows drives the strip. While the host has *not* claimed
the lamps (autonomous mode) the effect paints the light's own colour instead, so
it is safe to leave selected permanently.

## Binary Sensors

### Connection Status (all components)
```yaml
binary_sensor:
  - platform: hid_composite  # or hid_mouse, hid_keyboard, hid_telephony, hid_lamp_array
    type: connected
    name: "PC Connected"
```

### LampArray Status (hid_lamp_array)
```yaml
binary_sensor:
  - platform: hid_lamp_array
    type: autonomous
    name: "Lighting Self-Controlled"
```

### Telephony Status (hid_composite or hid_telephony)
```yaml
binary_sensor:
  - platform: hid_composite
    type: muted
    name: "Muted"

  - platform: hid_composite
    type: in_call
    name: "In Call"
  
  - platform: hid_composite
    type: ringing
    name: "Ringing"
```

## Switches

### Keep Awake (hid_composite, hid_mouse, hid_keyboard)
```yaml
switch:
  - platform: hid_composite
    type: mouse
    name: "Mouse Keep Awake"
    interval: 60s
    jitter: 10s
  
  - platform: hid_composite
    type: keyboard
    name: "Keyboard Keep Awake"
    key: "F15"
    interval: 60s
```

### Mute Control (hid_composite or hid_telephony)
```yaml
switch:
  - platform: hid_composite
    type: mute
    name: "Mute"
```

> **Note**: The mute switch syncs bidirectionally with the PC. If mute is toggled
> via Teams/Zoom, the switch updates automatically.
>
> The mute button is a toggle on the USB wire, so `mute`/`unmute` compare against
> the state the PC last reported and send nothing when it already matches. Until
> the PC has sent its first state report there is nothing to compare against, so
> the first press is sent unconditionally. `toggle_mute` always sends a press.

## Examples

See the [examples](examples/) folder:
- [basic.yaml](examples/basic.yaml) - Mouse
- [keyboard.yaml](examples/keyboard.yaml) - Keyboard
- [composite.yaml](examples/composite.yaml) - Mouse + Keyboard
- [test_composite_switch.yaml](examples/test_composite_switch.yaml) - Composite with switches
- [test_telephony.yaml](examples/test_telephony.yaml) - Telephony controls
- [lamp_array.yaml](examples/lamp_array.yaml) - LampArray driving a WS2812 strip
- [composite_lamp_array.yaml](examples/composite_lamp_array.yaml) - Mouse + Keyboard + LampArray

## License

MIT License. LampArray support derives from Microsoft's MIT-licensed
reference samples; see [NOTICE.md](NOTICE.md).
