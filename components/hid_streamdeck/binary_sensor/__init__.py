"""`connected`: the PC has the deck mounted and is not suspended."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import binary_sensor
from esphome.const import DEVICE_CLASS_CONNECTIVITY

from .. import CONF_HID_STREAMDECK_ID, HIDStreamDeck

DEPENDENCIES = ["hid_streamdeck"]
CONF_CONNECTED = "connected"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_HID_STREAMDECK_ID): cv.use_id(HIDStreamDeck),
        cv.Optional(CONF_CONNECTED): binary_sensor.binary_sensor_schema(device_class=DEVICE_CLASS_CONNECTIVITY),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_HID_STREAMDECK_ID])
    if CONF_CONNECTED in config:
        sens = await binary_sensor.new_binary_sensor(config[CONF_CONNECTED])
        cg.add(parent.set_connected_sensor(sens))
