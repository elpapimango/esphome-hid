"""One key of the deck as a button: pressing it in Home Assistant sends a key press (and, after `hold_time`, the release)."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import button

from .. import CONF_HID_STREAMDECK_ID, HIDStreamDeck, hid_streamdeck_ns

DEPENDENCIES = ["hid_streamdeck"]
CONF_KEY = "key"
CONF_ROW = "row"
CONF_COLUMN = "column"
CONF_HOLD_TIME = "hold_time"

ROWS, COLUMNS = 4, 8  # the XL: 4 rows x 8 columns, key index = row * 8 + column

HIDStreamDeckButton = hid_streamdeck_ns.class_("HIDStreamDeckButton", button.Button, cg.Component)


def _key(config):
    if CONF_KEY in config and (CONF_ROW in config or CONF_COLUMN in config):
        raise cv.Invalid("use `key`, or `row` and `column`, not both")
    if CONF_KEY not in config:
        if CONF_ROW not in config or CONF_COLUMN not in config:
            raise cv.Invalid("give `key` (0 to 31), or `row` and `column`")
        config = {**config, CONF_KEY: config[CONF_ROW] * COLUMNS + config[CONF_COLUMN]}
    return config


CONFIG_SCHEMA = cv.All(
    button.button_schema(HIDStreamDeckButton)
    .extend(
        {
            cv.GenerateID(CONF_HID_STREAMDECK_ID): cv.use_id(HIDStreamDeck),
            cv.Optional(CONF_KEY): cv.int_range(min=0, max=ROWS * COLUMNS - 1),
            cv.Optional(CONF_ROW): cv.int_range(min=0, max=ROWS - 1),
            cv.Optional(CONF_COLUMN): cv.int_range(min=0, max=COLUMNS - 1),
            cv.Optional(CONF_HOLD_TIME, default="80ms"): cv.All(
                cv.positive_time_period_milliseconds, cv.Range(min=cv.TimePeriod(milliseconds=10), max=cv.TimePeriod(seconds=5))
            ),
        }
    )
    .extend(cv.COMPONENT_SCHEMA),
    _key,
)


async def to_code(config):
    var = await button.new_button(config)
    await cg.register_component(var, config)
    parent = await cg.get_variable(config[CONF_HID_STREAMDECK_ID])
    cg.add(var.set_parent(parent))
    cg.add(var.set_key(config[CONF_KEY]))
    cg.add(var.set_hold_ms(config[CONF_HOLD_TIME]))
