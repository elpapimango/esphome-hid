"""ESPHome component: an ESP32-S3 that shows up on the PC as an Elgato Stream Deck XL.

The PC's Stream Deck software (or OpenDeck) sees a Stream Deck; in ESPHome each key is a button that Home Assistant can press,
which the host receives as a key press. What the host sends to the deck (key images, brightness) is counted and reported.
"""
import esphome.codegen as cg
import esphome.config_validation as cv
from esphome.const import CONF_ID

CODEOWNERS = []
DEPENDENCIES = ["esp32", "network"]
CONFLICTS_WITH = ["hid_mouse", "hid_keyboard", "hid_telephony", "hid_lamp_array", "hid_composite"]

CONF_SERIAL = "serial"
CONF_VENDOR_ID = "vendor_id"
CONF_PRODUCT_ID = "product_id"
CONF_HID_STREAMDECK_ID = "hid_streamdeck_id"
CONF_IMAGE_PORT = "image_port"
CONF_IMAGE_ROTATION = "image_rotation"

hid_streamdeck_ns = cg.esphome_ns.namespace("hid_streamdeck")
HIDStreamDeck = hid_streamdeck_ns.class_("HIDStreamDeck", cg.Component)

CONFIG_SCHEMA = cv.Schema(
    {
        cv.GenerateID(): cv.declare_id(HIDStreamDeck),
        # 12 characters, like a real deck's ("CL50K2A01008"). Default: made from the chip's MAC address.
        cv.Optional(CONF_SERIAL): cv.All(cv.string_strict, cv.Length(min=12, max=12)),
        # The ids of the real Stream Deck XL: the official software only talks to those. See the README before changing them
        # or distributing anything built with them.
        cv.Optional(CONF_VENDOR_ID, default=0x0FD9): cv.hex_uint16_t,
        cv.Optional(CONF_PRODUCT_ID, default=0x006C): cv.hex_uint16_t,
        # HTTP port that serves the last image the PC sent for each key, as /key/<n>.jpg (96x96 JPEG). 0 = off.
        cv.Optional(CONF_IMAGE_PORT, default=0): cv.port,
        # The deck software sends the XL's key images rotated 180 degrees. 180 serves them upright (lossless JPEG
        # rotation on the chip); 0 serves them as sent.
        cv.Optional(CONF_IMAGE_ROTATION, default=0): cv.one_of(0, 180, int=True),
    }
).extend(cv.COMPONENT_SCHEMA)


async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    if CONF_SERIAL in config:
        cg.add(var.set_serial(config[CONF_SERIAL]))
    cg.add(var.set_image_port(config[CONF_IMAGE_PORT]))
    cg.add(var.set_image_rotation(config[CONF_IMAGE_ROTATION]))
    cg.add(var.set_ids(config[CONF_VENDOR_ID], config[CONF_PRODUCT_ID]))
