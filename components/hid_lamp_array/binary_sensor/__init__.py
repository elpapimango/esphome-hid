import esphome.codegen as cg
from esphome.components import binary_sensor
import esphome.config_validation as cv
from esphome.const import DEVICE_CLASS_CONNECTIVITY

from .. import HIDLampArray, hid_lamp_array_ns

DEPENDENCIES = ["hid_lamp_array"]
CODEOWNERS = ["@AntorFr"]

CONF_HID_LAMP_ARRAY_ID = "hid_lamp_array_id"

CONF_TYPE_CONNECTED = "connected"
CONF_TYPE_AUTONOMOUS = "autonomous"

LampArrayConnectedBinarySensor = hid_lamp_array_ns.class_(
    "LampArrayConnectedBinarySensor", binary_sensor.BinarySensor, cg.PollingComponent
)
LampArrayAutonomousBinarySensor = hid_lamp_array_ns.class_(
    "LampArrayAutonomousBinarySensor", binary_sensor.BinarySensor, cg.Component
)

BASE_SCHEMA = {
    cv.GenerateID(CONF_HID_LAMP_ARRAY_ID): cv.use_id(HIDLampArray),
}

TYPES = {
    CONF_TYPE_CONNECTED: binary_sensor.binary_sensor_schema(
        LampArrayConnectedBinarySensor,
        device_class=DEVICE_CLASS_CONNECTIVITY,
    )
    .extend(cv.polling_component_schema("1s"))
    .extend(BASE_SCHEMA),
    CONF_TYPE_AUTONOMOUS: binary_sensor.binary_sensor_schema(
        LampArrayAutonomousBinarySensor,
    )
    .extend(cv.COMPONENT_SCHEMA)
    .extend(BASE_SCHEMA),
}

CONFIG_SCHEMA = cv.typed_schema(TYPES, lower=True)


async def to_code(config):
    var = await binary_sensor.new_binary_sensor(config)
    await cg.register_component(var, config)

    parent = await cg.get_variable(config[CONF_HID_LAMP_ARRAY_ID])
    cg.add(var.set_parent(parent))
