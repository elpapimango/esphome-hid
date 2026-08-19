import esphome.codegen as cg
from esphome import automation
from esphome.components.light.effects import register_addressable_effect
from esphome.components.light.types import AddressableLightEffect
import esphome.config_validation as cv
from esphome.const import CONF_ID, CONF_NAME, CONF_OFFSET

CODEOWNERS = ["@AntorFr"]
DEPENDENCIES = ["esp32"]

# Cannot be used with other HID components (each configures USB)
CONFLICTS_WITH = ["hid_mouse", "hid_keyboard", "hid_telephony", "hid_composite"]

hid_lamp_array_ns = cg.esphome_ns.namespace("hid_lamp_array")
Color = cg.esphome_ns.class_("Color")
HIDLampArray = hid_lamp_array_ns.class_("HIDLampArray", cg.Component)
LampArrayLightEffect = hid_lamp_array_ns.class_(
    "LampArrayLightEffect", AddressableLightEffect
)
LampUpdateTrigger = hid_lamp_array_ns.class_(
    "LampUpdateTrigger", automation.Trigger.template(cg.uint16, Color)
)
AutonomousModeTrigger = hid_lamp_array_ns.class_(
    "AutonomousModeTrigger", automation.Trigger.template(cg.bool_)
)

CONF_LAMP_ARRAY_ID = "lamp_array_id"
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
CONF_X = "x"
CONF_Y = "y"
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


LAMP_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_X): millimeters,
        cv.Required(CONF_Y): millimeters,
        cv.Optional(CONF_Z, default=0): millimeters,
        cv.Optional(CONF_PURPOSES): validate_purposes,
    }
)


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
LAMP_ARRAY_SCHEMA = cv.Schema(
    {
        cv.Required(CONF_LAMP_COUNT): cv.int_range(min=1, max=1024),
        cv.Optional(CONF_KIND, default="PERIPHERAL"): cv.enum(
            LAMP_ARRAY_KINDS, upper=True
        ),
        cv.Optional(CONF_WIDTH, default=0): millimeters,
        cv.Optional(CONF_HEIGHT, default=0): millimeters,
        cv.Optional(CONF_DEPTH, default=0): millimeters,
        cv.Optional(CONF_ROWS, default=1): cv.int_range(min=1, max=1024),
        cv.Optional(
            CONF_MIN_UPDATE_INTERVAL, default="33ms"
        ): cv.positive_time_period_milliseconds,
        cv.Optional(
            CONF_UPDATE_LATENCY, default="33ms"
        ): cv.positive_time_period_milliseconds,
        cv.Optional(CONF_PURPOSES, default=["ACCENT"]): validate_purposes,
        cv.Optional(CONF_INTENSITY_LEVELS, default=1): cv.int_range(min=1, max=255),
        cv.Optional(CONF_LAMPS): cv.ensure_list(LAMP_SCHEMA),
        cv.Optional(CONF_ON_LAMP_UPDATE): automation.validate_automation(
            {cv.GenerateID(CONF_ID): cv.declare_id(LampUpdateTrigger)}
        ),
        cv.Optional(CONF_ON_AUTONOMOUS_MODE): automation.validate_automation(
            {cv.GenerateID(CONF_ID): cv.declare_id(AutonomousModeTrigger)}
        ),
    }
)

CONFIG_SCHEMA = cv.All(
    LAMP_ARRAY_SCHEMA.extend(
        {
            cv.GenerateID(): cv.declare_id(HIDLampArray),
        }
    ).extend(cv.COMPONENT_SCHEMA),
    validate_lamp_list,
)


async def setup_lamp_array_core(var, config):
    """Push the LampArray geometry onto a component exposing core()."""
    core = var.core()
    cg.add(core.set_lamp_count(config[CONF_LAMP_COUNT]))
    cg.add(
        core.set_bounding_box_mm(
            config[CONF_WIDTH], config[CONF_HEIGHT], config[CONF_DEPTH]
        )
    )
    cg.add(core.set_rows(config[CONF_ROWS]))
    cg.add(core.set_kind(config[CONF_KIND]))
    cg.add(
        core.set_min_update_interval_ms(
            config[CONF_MIN_UPDATE_INTERVAL].total_milliseconds
        )
    )
    cg.add(core.set_update_latency_ms(config[CONF_UPDATE_LATENCY].total_milliseconds))
    cg.add(core.set_default_purposes(config[CONF_PURPOSES]))
    cg.add(core.set_intensity_levels(config[CONF_INTENSITY_LEVELS]))
    for lamp in config.get(CONF_LAMPS, []):
        cg.add(
            core.add_lamp(
                lamp[CONF_X],
                lamp[CONF_Y],
                lamp[CONF_Z],
                lamp.get(CONF_PURPOSES, config[CONF_PURPOSES]),
                0,
            )
        )


async def setup_lamp_array_triggers(var, config):
    for conf in config.get(CONF_ON_LAMP_UPDATE, []):
        trigger = cg.new_Pvariable(conf[CONF_ID], var)
        await automation.build_automation(
            trigger,
            [(cg.uint16, "lamp_id"), (Color, "color")],
            conf,
        )
    for conf in config.get(CONF_ON_AUTONOMOUS_MODE, []):
        trigger = cg.new_Pvariable(conf[CONF_ID], var)
        await automation.build_automation(trigger, [(cg.bool_, "autonomous")], conf)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await setup_lamp_array_core(var, config)
    await setup_lamp_array_triggers(var, config)


@register_addressable_effect(
    "lamp_array",
    LampArrayLightEffect,
    "LampArray",
    {
        cv.GenerateID(CONF_LAMP_ARRAY_ID): cv.use_id(HIDLampArray),
        cv.Optional(CONF_OFFSET, default=0): cv.int_range(min=0, max=65535),
    },
)
async def lamp_array_light_effect_to_code(config, effect_id):
    parent = await cg.get_variable(config[CONF_LAMP_ARRAY_ID])

    effect = cg.new_Pvariable(effect_id, config[CONF_NAME])
    cg.add(effect.set_lamp_array(parent))
    cg.add(effect.set_offset(config[CONF_OFFSET]))
    return effect
