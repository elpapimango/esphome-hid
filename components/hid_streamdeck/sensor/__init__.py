"""What the host sends to the deck: the brightness it sets (percent) and how many key images it has sent."""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.components import sensor
from esphome.const import STATE_CLASS_MEASUREMENT, STATE_CLASS_TOTAL_INCREASING, UNIT_PERCENT

from .. import CONF_HID_STREAMDECK_ID, HIDStreamDeck

DEPENDENCIES = ["hid_streamdeck"]
CONF_BRIGHTNESS = "brightness"
CONF_IMAGES = "images"

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(CONF_HID_STREAMDECK_ID): cv.use_id(HIDStreamDeck),
        cv.Optional(CONF_BRIGHTNESS): sensor.sensor_schema(
            unit_of_measurement=UNIT_PERCENT, accuracy_decimals=0, state_class=STATE_CLASS_MEASUREMENT, icon="mdi:brightness-6"
        ),
        cv.Optional(CONF_IMAGES): sensor.sensor_schema(
            accuracy_decimals=0, state_class=STATE_CLASS_TOTAL_INCREASING, icon="mdi:image-multiple"
        ),
    }
)


async def to_code(config):
    parent = await cg.get_variable(config[CONF_HID_STREAMDECK_ID])
    if CONF_BRIGHTNESS in config:
        cg.add(parent.set_brightness_sensor(await sensor.new_sensor(config[CONF_BRIGHTNESS])))
    if CONF_IMAGES in config:
        cg.add(parent.set_images_sensor(await sensor.new_sensor(config[CONF_IMAGES])))
