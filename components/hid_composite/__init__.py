import esphome.codegen as cg
import esphome.config_validation as cv
from esphome import automation
from esphome.components.light.effects import register_addressable_effect
from esphome.components.light.types import AddressableLightEffect
from esphome.const import CONF_EFFECTS, CONF_ID, CONF_LIGHT, CONF_NAME, CONF_OFFSET
import esphome.final_validate as fv

CODEOWNERS = ["@elpapimango"]
DEPENDENCIES = ["esp32"]
CONFLICTS_WITH = ["hid_mouse", "hid_keyboard", "hid_telephony", "hid_lamp_array"]

CONF_LAYOUT = "layout"

hid_composite_ns = cg.esphome_ns.namespace("hid_composite")
Color = cg.esphome_ns.class_("Color")
HIDComposite = hid_composite_ns.class_("HIDComposite", cg.Component)

# Keyboard layout enum
KeyboardLayout = hid_composite_ns.enum("KeyboardLayout")
KEYBOARD_LAYOUTS = {
    "QWERTY_US": KeyboardLayout.LAYOUT_QWERTY_US,
    "AZERTY_FR": KeyboardLayout.LAYOUT_AZERTY_FR,
    "QWERTZ_DE": KeyboardLayout.LAYOUT_QWERTZ_DE,
}

# Mouse Actions
MoveAction = hid_composite_ns.class_("MoveAction", automation.Action)
ScrollAction = hid_composite_ns.class_("ScrollAction", automation.Action)
ClickAction = hid_composite_ns.class_("ClickAction", automation.Action)
MousePressAction = hid_composite_ns.class_("MousePressAction", automation.Action)
MouseReleaseAction = hid_composite_ns.class_("MouseReleaseAction", automation.Action)
MouseReleaseAllAction = hid_composite_ns.class_("MouseReleaseAllAction", automation.Action)

# Keyboard Actions
KeyPressAction = hid_composite_ns.class_("KeyPressAction", automation.Action)
KeyReleaseAction = hid_composite_ns.class_("KeyReleaseAction", automation.Action)
KeyTapAction = hid_composite_ns.class_("KeyTapAction", automation.Action)
KeyReleaseAllAction = hid_composite_ns.class_("KeyReleaseAllAction", automation.Action)
TypeAction = hid_composite_ns.class_("TypeAction", automation.Action)

# Keep Awake Actions
StartMouseKeepAwakeAction = hid_composite_ns.class_("StartMouseKeepAwakeAction", automation.Action)
StopMouseKeepAwakeAction = hid_composite_ns.class_("StopMouseKeepAwakeAction", automation.Action)
StartKeyboardKeepAwakeAction = hid_composite_ns.class_("StartKeyboardKeepAwakeAction", automation.Action)
StopKeyboardKeepAwakeAction = hid_composite_ns.class_("StopKeyboardKeepAwakeAction", automation.Action)

# Telephony Actions
MuteAction = hid_composite_ns.class_("MuteAction", automation.Action)
UnmuteAction = hid_composite_ns.class_("UnmuteAction", automation.Action)
ToggleMuteAction = hid_composite_ns.class_("ToggleMuteAction", automation.Action)
MuteTelephonyAction = hid_composite_ns.class_("MuteTelephonyAction", automation.Action)
MuteConsumerAction = hid_composite_ns.class_("MuteConsumerAction", automation.Action)
MuteTeamsAction = hid_composite_ns.class_("MuteTeamsAction", automation.Action)
HookSwitchAction = hid_composite_ns.class_("HookSwitchAction", automation.Action)
AnswerCallAction = hid_composite_ns.class_("AnswerCallAction", automation.Action)
HangUpAction = hid_composite_ns.class_("HangUpAction", automation.Action)
VolumeUpAction = hid_composite_ns.class_("VolumeUpAction", automation.Action)
VolumeDownAction = hid_composite_ns.class_("VolumeDownAction", automation.Action)

CONF_X = "x"
CONF_Y = "y"
CONF_VERTICAL = "vertical"
CONF_HORIZONTAL = "horizontal"
CONF_BUTTON = "button"
CONF_KEY = "key"
CONF_MODIFIERS = "modifiers"
CONF_TEXT = "text"
CONF_INTERVAL = "interval"

MOUSE_BUTTONS = {
    "LEFT": 0,
    "RIGHT": 1,
    "MIDDLE": 2,
}

MODIFIERS = {
    "NONE": 0x00,
    "CTRL": 0x01, "LEFT_CTRL": 0x01, "LCTRL": 0x01,
    "SHIFT": 0x02, "LEFT_SHIFT": 0x02, "LSHIFT": 0x02,
    "ALT": 0x04, "LEFT_ALT": 0x04, "LALT": 0x04,
    "GUI": 0x08, "LEFT_GUI": 0x08, "LGUI": 0x08, "WIN": 0x08, "CMD": 0x08, "META": 0x08,
    "RIGHT_CTRL": 0x10, "RCTRL": 0x10,
    "RIGHT_SHIFT": 0x20, "RSHIFT": 0x20,
    "RIGHT_ALT": 0x40, "RALT": 0x40,
    "RIGHT_GUI": 0x80, "RGUI": 0x80,
    "CTRL_SHIFT": 0x03, "CTRL_ALT": 0x05, "CTRL_GUI": 0x09,
    "SHIFT_ALT": 0x06, "SHIFT_GUI": 0x0A, "ALT_GUI": 0x0C,
    "CTRL_SHIFT_ALT": 0x07, "CTRL_SHIFT_GUI": 0x0B, "CTRL_ALT_GUI": 0x0D,
    "SHIFT_ALT_GUI": 0x0E, "CTRL_SHIFT_ALT_GUI": 0x0F,
}

def validate_button(value):
    if isinstance(value, str):
        upper = value.upper()
        if upper in MOUSE_BUTTONS:
            return MOUSE_BUTTONS[upper]
    return cv.int_range(min=0, max=2)(value)

def validate_modifiers(value):
    if isinstance(value, int):
        return cv.int_range(min=0, max=255)(value)
    if isinstance(value, str):
        upper = value.upper()
        if upper in MODIFIERS:
            return MODIFIERS[upper]
        raise cv.Invalid(f"Unknown modifier: {value}")
    raise cv.Invalid(f"Invalid modifier type: {type(value)}")

# ============ LampArray (Windows Dynamic Lighting) ============
# Optional: adds report IDs 6-11 to the composite descriptor so the PC can push
# per-lamp colours to us. Kept in sync with the hid_lamp_array component.

LampArrayLightEffect = hid_composite_ns.class_(
    "LampArrayLightEffect", AddressableLightEffect
)
LampUpdateTrigger = hid_composite_ns.class_(
    "LampUpdateTrigger", automation.Trigger.template(cg.uint16, Color)
)
AutonomousModeTrigger = hid_composite_ns.class_(
    "AutonomousModeTrigger", automation.Trigger.template(cg.bool_)
)

CONF_LAMP_ARRAY = "lamp_array"
CONF_HID_COMPOSITE_ID = "hid_composite_id"
CONF_LAMP_COUNT = "lamp_count"
CONF_KIND = "kind"
CONF_WIDTH = "width"
CONF_HEIGHT = "height"
CONF_DEPTH = "depth"
CONF_ROWS = "rows"
CONF_MIN_UPDATE_INTERVAL = "min_update_interval"
CONF_UPDATE_LATENCY = "update_latency"
CONF_PURPOSES = "purposes"
CONF_INTENSITY_LEVELS = "intensity_levels"
CONF_LAMPS = "lamps"
CONF_Z = "z"
CONF_ON_LAMP_UPDATE = "on_lamp_update"
CONF_ON_AUTONOMOUS_MODE = "on_autonomous_mode"

# LampArrayKind, HID Usage Tables 1.4. Tells the host what it is lighting up.
LAMP_ARRAY_KINDS = {
    "KEYBOARD": 1,
    "MOUSE": 2,
    "GAME_CONTROLLER": 3,
    "PERIPHERAL": 4,
    "SCENE": 5,
    "NOTIFICATION": 6,
    "CHASSIS": 7,
    "WEARABLE": 8,
    "FURNITURE": 9,
    "ART": 10,
}

# LampPurposes bitfield.
LAMP_PURPOSES = {
    "CONTROL": 0x01,
    "ACCENT": 0x02,
    "BRANDING": 0x04,
    "STATUS": 0x08,
    "ILLUMINATION": 0x10,
    "PRESENTATION": 0x20,
}


def validate_purposes(value):
    value = cv.ensure_list(cv.one_of(*LAMP_PURPOSES, upper=True))(value)
    if not value:
        raise cv.Invalid("At least one purpose is required")
    mask = 0
    for purpose in value:
        mask |= LAMP_PURPOSES[purpose]
    return mask


# Positions are millimetres on the wire (converted to micrometres in C++), so
# accept plain numbers and distance strings alike.
def millimeters(value):
    if isinstance(value, str):
        value = value.strip()
        for suffix, factor in (("mm", 1), ("cm", 10), ("m", 1000)):
            if value.endswith(suffix):
                return int(float(value[: -len(suffix)].strip()) * factor)
    return cv.uint32_t(value)


LAMP_SCHEMA = cv.Schema({
    cv.Required(CONF_X): millimeters,
    cv.Required(CONF_Y): millimeters,
    cv.Optional(CONF_Z, default=0): millimeters,
    cv.Optional(CONF_PURPOSES): validate_purposes,
})


def validate_lamp_list(config):
    lamps = config.get(CONF_LAMPS)
    if lamps is not None and len(lamps) != config[CONF_LAMP_COUNT]:
        raise cv.Invalid(
            f"'{CONF_LAMPS}' has {len(lamps)} entries but "
            f"'{CONF_LAMP_COUNT}' is {config[CONF_LAMP_COUNT]}"
        )
    if config[CONF_ROWS] > config[CONF_LAMP_COUNT]:
        raise cv.Invalid(f"'{CONF_ROWS}' cannot exceed '{CONF_LAMP_COUNT}'")
    # A zero-size bounding box makes the host place every lamp at the origin,
    # which breaks its spatial effects, so insist on real measurements.
    if config[CONF_WIDTH] == 0 and config[CONF_HEIGHT] == 0:
        raise cv.Invalid(
            f"Set at least one of '{CONF_WIDTH}' or '{CONF_HEIGHT}' to the "
            "physical size of the lamp area"
        )
    return config


# Two colour caches at 4 bytes/lamp plus the host's own bookkeeping, so cap the
# count well below the 65535 the descriptor allows.
LAMP_ARRAY_SCHEMA = cv.All(cv.Schema({
    cv.Required(CONF_LAMP_COUNT): cv.int_range(min=1, max=1024),
    cv.Optional(CONF_KIND, default="PERIPHERAL"): cv.enum(LAMP_ARRAY_KINDS, upper=True),
    cv.Optional(CONF_WIDTH, default=0): millimeters,
    cv.Optional(CONF_HEIGHT, default=0): millimeters,
    cv.Optional(CONF_DEPTH, default=0): millimeters,
    cv.Optional(CONF_ROWS, default=1): cv.int_range(min=1, max=1024),
    cv.Optional(CONF_MIN_UPDATE_INTERVAL, default="33ms"): cv.positive_time_period_milliseconds,
    cv.Optional(CONF_UPDATE_LATENCY, default="33ms"): cv.positive_time_period_milliseconds,
    cv.Optional(CONF_PURPOSES, default=["ACCENT"]): validate_purposes,
    cv.Optional(CONF_INTENSITY_LEVELS, default=1): cv.int_range(min=1, max=255),
    cv.Optional(CONF_LAMPS): cv.ensure_list(LAMP_SCHEMA),
    cv.Optional(CONF_ON_LAMP_UPDATE): automation.validate_automation(
        {cv.GenerateID(CONF_ID): cv.declare_id(LampUpdateTrigger)}
    ),
    cv.Optional(CONF_ON_AUTONOMOUS_MODE): automation.validate_automation(
        {cv.GenerateID(CONF_ID): cv.declare_id(AutonomousModeTrigger)}
    ),
}), validate_lamp_list)

CONFIG_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.declare_id(HIDComposite),
    cv.Optional(CONF_LAYOUT, default="QWERTY_US"): cv.enum(KEYBOARD_LAYOUTS, upper=True),
    cv.Optional(CONF_LAMP_ARRAY): LAMP_ARRAY_SCHEMA,
}).extend(cv.COMPONENT_SCHEMA)


def _final_validate(config):
    """Reject the lamp_array light effect when no lamps are configured.

    Without lamp_array: the descriptor carries no lamp reports and the effect
    class is not compiled, so catch it here rather than in the C++ build.
    """
    if CONF_LAMP_ARRAY in config:
        return

    full_config = fv.full_config.get()
    for light_conf in full_config.get(CONF_LIGHT, []):
        for effect in light_conf.get(CONF_EFFECTS, []):
            if "lamp_array" in effect:
                raise cv.Invalid(
                    "The 'lamp_array' light effect requires a 'lamp_array:' "
                    "block under 'hid_composite:'"
                )


FINAL_VALIDATE_SCHEMA = _final_validate


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    cg.add(var.set_layout(config[CONF_LAYOUT]))

    lamp_array = config.get(CONF_LAMP_ARRAY)
    if lamp_array is None:
        return

    # Gates the descriptor block, the report handlers and the light effect.
    cg.add_define("USE_HID_COMPOSITE_LAMP_ARRAY")

    core = var.core()
    cg.add(core.set_lamp_count(lamp_array[CONF_LAMP_COUNT]))
    cg.add(core.set_bounding_box_mm(
        lamp_array[CONF_WIDTH], lamp_array[CONF_HEIGHT], lamp_array[CONF_DEPTH]
    ))
    cg.add(core.set_rows(lamp_array[CONF_ROWS]))
    cg.add(core.set_kind(lamp_array[CONF_KIND]))
    cg.add(core.set_min_update_interval_ms(
        lamp_array[CONF_MIN_UPDATE_INTERVAL].total_milliseconds
    ))
    cg.add(core.set_update_latency_ms(
        lamp_array[CONF_UPDATE_LATENCY].total_milliseconds
    ))
    cg.add(core.set_default_purposes(lamp_array[CONF_PURPOSES]))
    cg.add(core.set_intensity_levels(lamp_array[CONF_INTENSITY_LEVELS]))
    for lamp in lamp_array.get(CONF_LAMPS, []):
        cg.add(core.add_lamp(
            lamp[CONF_X],
            lamp[CONF_Y],
            lamp[CONF_Z],
            lamp.get(CONF_PURPOSES, lamp_array[CONF_PURPOSES]),
            0,
        ))

    for conf in lamp_array.get(CONF_ON_LAMP_UPDATE, []):
        trigger = cg.new_Pvariable(conf[CONF_ID], var)
        await automation.build_automation(
            trigger, [(cg.uint16, "lamp_id"), (Color, "color")], conf
        )
    for conf in lamp_array.get(CONF_ON_AUTONOMOUS_MODE, []):
        trigger = cg.new_Pvariable(conf[CONF_ID], var)
        await automation.build_automation(trigger, [(cg.bool_, "autonomous")], conf)


@register_addressable_effect(
    "lamp_array",
    LampArrayLightEffect,
    "LampArray",
    {
        cv.GenerateID(CONF_HID_COMPOSITE_ID): cv.use_id(HIDComposite),
        cv.Optional(CONF_OFFSET, default=0): cv.int_range(min=0, max=65535),
    },
)
async def lamp_array_light_effect_to_code(config, effect_id):
    parent = await cg.get_variable(config[CONF_HID_COMPOSITE_ID])

    effect = cg.new_Pvariable(effect_id, config[CONF_NAME])
    cg.add(effect.set_lamp_array(parent))
    cg.add(effect.set_offset(config[CONF_OFFSET]))
    return effect

# ============ Mouse Actions ============

MOVE_ACTION_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.use_id(HIDComposite),
    cv.Required(CONF_X): cv.templatable(cv.int_range(min=-127, max=127)),
    cv.Required(CONF_Y): cv.templatable(cv.int_range(min=-127, max=127)),
})

@automation.register_action("hid_composite.move", MoveAction, MOVE_ACTION_SCHEMA)
async def move_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    template_ = await cg.templatable(config[CONF_X], args, cg.int_)
    cg.add(var.set_x(template_))
    template_ = await cg.templatable(config[CONF_Y], args, cg.int_)
    cg.add(var.set_y(template_))
    return var

SCROLL_ACTION_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.use_id(HIDComposite),
    cv.Optional(CONF_VERTICAL, default=0): cv.templatable(cv.int_range(min=-127, max=127)),
    cv.Optional(CONF_HORIZONTAL, default=0): cv.templatable(cv.int_range(min=-127, max=127)),
})

@automation.register_action("hid_composite.scroll", ScrollAction, SCROLL_ACTION_SCHEMA)
async def scroll_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    template_ = await cg.templatable(config[CONF_VERTICAL], args, cg.int_)
    cg.add(var.set_vertical(template_))
    template_ = await cg.templatable(config[CONF_HORIZONTAL], args, cg.int_)
    cg.add(var.set_horizontal(template_))
    return var

CLICK_ACTION_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.use_id(HIDComposite),
    cv.Optional(CONF_BUTTON, default="LEFT"): cv.templatable(validate_button),
})

@automation.register_action("hid_composite.click", ClickAction, CLICK_ACTION_SCHEMA)
async def click_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    template_ = await cg.templatable(config[CONF_BUTTON], args, cg.uint8)
    cg.add(var.set_button(template_))
    return var

MOUSE_PRESS_ACTION_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.use_id(HIDComposite),
    cv.Optional(CONF_BUTTON, default="LEFT"): cv.templatable(validate_button),
})

@automation.register_action("hid_composite.mouse_press", MousePressAction, MOUSE_PRESS_ACTION_SCHEMA)
async def mouse_press_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    template_ = await cg.templatable(config[CONF_BUTTON], args, cg.uint8)
    cg.add(var.set_button(template_))
    return var

MOUSE_RELEASE_ACTION_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.use_id(HIDComposite),
    cv.Optional(CONF_BUTTON, default="LEFT"): cv.templatable(validate_button),
})

@automation.register_action("hid_composite.mouse_release", MouseReleaseAction, MOUSE_RELEASE_ACTION_SCHEMA)
async def mouse_release_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    template_ = await cg.templatable(config[CONF_BUTTON], args, cg.uint8)
    cg.add(var.set_button(template_))
    return var

MOUSE_RELEASE_ALL_ACTION_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.use_id(HIDComposite),
})

@automation.register_action("hid_composite.mouse_release_all", MouseReleaseAllAction, MOUSE_RELEASE_ALL_ACTION_SCHEMA)
async def mouse_release_all_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var

# ============ Keyboard Actions ============

KEY_PRESS_ACTION_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.use_id(HIDComposite),
    cv.Required(CONF_KEY): cv.templatable(cv.string),
    cv.Optional(CONF_MODIFIERS, default="NONE"): validate_modifiers,
})

@automation.register_action("hid_composite.key_press", KeyPressAction, KEY_PRESS_ACTION_SCHEMA)
async def key_press_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    template_ = await cg.templatable(config[CONF_KEY], args, cg.std_string)
    cg.add(var.set_key(template_))
    cg.add(var.set_modifier(config[CONF_MODIFIERS]))
    return var

KEY_RELEASE_ACTION_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.use_id(HIDComposite),
})

@automation.register_action("hid_composite.key_release", KeyReleaseAction, KEY_RELEASE_ACTION_SCHEMA)
async def key_release_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var

KEY_TAP_ACTION_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.use_id(HIDComposite),
    cv.Required(CONF_KEY): cv.templatable(cv.string),
    cv.Optional(CONF_MODIFIERS, default="NONE"): validate_modifiers,
})

@automation.register_action("hid_composite.key_tap", KeyTapAction, KEY_TAP_ACTION_SCHEMA)
async def key_tap_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    template_ = await cg.templatable(config[CONF_KEY], args, cg.std_string)
    cg.add(var.set_key(template_))
    cg.add(var.set_modifier(config[CONF_MODIFIERS]))
    return var

KEY_RELEASE_ALL_ACTION_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.use_id(HIDComposite),
})

@automation.register_action("hid_composite.key_release_all", KeyReleaseAllAction, KEY_RELEASE_ALL_ACTION_SCHEMA)
async def key_release_all_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var

CONF_SPEED = "speed"
CONF_JITTER = "jitter"

TYPE_ACTION_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.use_id(HIDComposite),
    cv.Required(CONF_TEXT): cv.templatable(cv.string),
    cv.Optional(CONF_SPEED, default=50): cv.templatable(cv.positive_int),
    cv.Optional(CONF_JITTER, default=0): cv.templatable(cv.positive_int),
})

@automation.register_action("hid_composite.type", TypeAction, TYPE_ACTION_SCHEMA)
async def type_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    template_ = await cg.templatable(config[CONF_TEXT], args, cg.std_string)
    cg.add(var.set_text(template_))
    speed = await cg.templatable(config[CONF_SPEED], args, cg.uint32)
    cg.add(var.set_speed(speed))
    jitter = await cg.templatable(config[CONF_JITTER], args, cg.uint32)
    cg.add(var.set_jitter(jitter))
    return var


# ============ Keep Awake Actions ============

START_MOUSE_KEEP_AWAKE_ACTION_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.use_id(HIDComposite),
    cv.Optional(CONF_INTERVAL, default="60s"): cv.templatable(cv.positive_time_period_milliseconds),
    cv.Optional(CONF_JITTER, default="0s"): cv.templatable(cv.positive_time_period_milliseconds),
})

@automation.register_action("hid_composite.start_mouse_keep_awake", StartMouseKeepAwakeAction, START_MOUSE_KEEP_AWAKE_ACTION_SCHEMA)
async def start_mouse_keep_awake_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    template_ = await cg.templatable(config[CONF_INTERVAL], args, cg.uint32)
    cg.add(var.set_interval(template_))
    template_ = await cg.templatable(config[CONF_JITTER], args, cg.uint32)
    cg.add(var.set_jitter(template_))
    return var

STOP_MOUSE_KEEP_AWAKE_ACTION_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.use_id(HIDComposite),
})

@automation.register_action("hid_composite.stop_mouse_keep_awake", StopMouseKeepAwakeAction, STOP_MOUSE_KEEP_AWAKE_ACTION_SCHEMA)
async def stop_mouse_keep_awake_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var

START_KEYBOARD_KEEP_AWAKE_ACTION_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.use_id(HIDComposite),
    cv.Required(CONF_KEY): cv.templatable(cv.string),
    cv.Optional(CONF_INTERVAL, default="60s"): cv.templatable(cv.positive_time_period_milliseconds),
    cv.Optional(CONF_JITTER, default="0s"): cv.templatable(cv.positive_time_period_milliseconds),
})

@automation.register_action("hid_composite.start_keyboard_keep_awake", StartKeyboardKeepAwakeAction, START_KEYBOARD_KEEP_AWAKE_ACTION_SCHEMA)
async def start_keyboard_keep_awake_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    template_ = await cg.templatable(config[CONF_KEY], args, cg.std_string)
    cg.add(var.set_key(template_))
    template_ = await cg.templatable(config[CONF_INTERVAL], args, cg.uint32)
    cg.add(var.set_interval(template_))
    template_ = await cg.templatable(config[CONF_JITTER], args, cg.uint32)
    cg.add(var.set_jitter(template_))
    return var

STOP_KEYBOARD_KEEP_AWAKE_ACTION_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.use_id(HIDComposite),
})

@automation.register_action("hid_composite.stop_keyboard_keep_awake", StopKeyboardKeepAwakeAction, STOP_KEYBOARD_KEEP_AWAKE_ACTION_SCHEMA)
async def stop_keyboard_keep_awake_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var


# ============ Telephony Actions ============

MUTE_ACTION_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.use_id(HIDComposite),
})

@automation.register_action("hid_composite.mute", MuteAction, MUTE_ACTION_SCHEMA)
async def mute_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var

UNMUTE_ACTION_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.use_id(HIDComposite),
})

@automation.register_action("hid_composite.unmute", UnmuteAction, UNMUTE_ACTION_SCHEMA)
async def unmute_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var

TOGGLE_MUTE_ACTION_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.use_id(HIDComposite),
})

@automation.register_action("hid_composite.toggle_mute", ToggleMuteAction, TOGGLE_MUTE_ACTION_SCHEMA)
async def toggle_mute_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var

# Mute Telephony only (Page 0x0B) - for testing
MUTE_TELEPHONY_ACTION_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.use_id(HIDComposite),
})

@automation.register_action("hid_composite.mute_telephony", MuteTelephonyAction, MUTE_TELEPHONY_ACTION_SCHEMA)
async def mute_telephony_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var

# Mute Consumer only (Page 0x0C) - system volume mute
MUTE_CONSUMER_ACTION_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.use_id(HIDComposite),
})

@automation.register_action("hid_composite.mute_consumer", MuteConsumerAction, MUTE_CONSUMER_ACTION_SCHEMA)
async def mute_consumer_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var

# Mute Teams - sends Ctrl+Shift+M keyboard shortcut
MUTE_TEAMS_ACTION_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.use_id(HIDComposite),
})

@automation.register_action("hid_composite.mute_teams", MuteTeamsAction, MUTE_TEAMS_ACTION_SCHEMA)
async def mute_teams_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var

VOLUME_UP_ACTION_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.use_id(HIDComposite),
})

@automation.register_action("hid_composite.volume_up", VolumeUpAction, VOLUME_UP_ACTION_SCHEMA)
async def volume_up_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var

VOLUME_DOWN_ACTION_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.use_id(HIDComposite),
})

@automation.register_action("hid_composite.volume_down", VolumeDownAction, VOLUME_DOWN_ACTION_SCHEMA)
async def volume_down_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var

CONF_STATE = "state"

HOOK_SWITCH_ACTION_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.use_id(HIDComposite),
    cv.Required(CONF_STATE): cv.templatable(cv.boolean),
})

@automation.register_action("hid_composite.hook_switch", HookSwitchAction, HOOK_SWITCH_ACTION_SCHEMA)
async def hook_switch_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    template_ = await cg.templatable(config[CONF_STATE], args, cg.bool_)
    cg.add(var.set_state(template_))
    return var

ANSWER_CALL_ACTION_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.use_id(HIDComposite),
})

@automation.register_action("hid_composite.answer_call", AnswerCallAction, ANSWER_CALL_ACTION_SCHEMA)
async def answer_call_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var

HANG_UP_ACTION_SCHEMA = cv.Schema({
    cv.GenerateID(): cv.use_id(HIDComposite),
})

@automation.register_action("hid_composite.hang_up", HangUpAction, HANG_UP_ACTION_SCHEMA)
async def hang_up_action_to_code(config, action_id, template_arg, args):
    var = cg.new_Pvariable(action_id, template_arg)
    await cg.register_parented(var, config[CONF_ID])
    return var
