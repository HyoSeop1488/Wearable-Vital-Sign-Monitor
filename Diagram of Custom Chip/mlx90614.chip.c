#include "wokwi-api.h"
#include <stdio.h>
#include <stdlib.h>

typedef struct {
  uint8_t current_reg;
  uint8_t read_count;
  uint32_t ambient_temp_attr;
  uint32_t object_temp_attr;
} chip_state_t;

static bool on_connect(void* user_data, uint32_t address, bool rw) {
  chip_state_t* chip = (chip_state_t*)user_data;
  chip->read_count = 0;
  return true;
}

static bool on_write(void* user_data, uint8_t data) {
  chip_state_t* chip = (chip_state_t*)user_data;
  chip->current_reg = data;
  chip->read_count = 0;
  return true;
}

static uint8_t on_read(void* user_data) {
  chip_state_t* chip = (chip_state_t*)user_data;
  float T_celsius;

  if (chip->current_reg == 0x06) {
    T_celsius = attr_read_float(chip->ambient_temp_attr);
  } else if (chip->current_reg == 0x07) {
    T_celsius = attr_read_float(chip->object_temp_attr);
  } else {
    return 0;
  }

  float T_kelvin = T_celsius + 273.15f;
  uint16_t raw_value = (uint16_t)(T_kelvin * 50.0f + 0.5f);
  uint8_t low_byte = raw_value & 0xFF;
  uint8_t high_byte = (raw_value >> 8) & 0xFF;

  if (chip->read_count == 0) {
    chip->read_count++;
    return low_byte;
  } else if (chip->read_count == 1) {
    chip->read_count = 0;
    return high_byte;
  }
  return 0;
}

static void on_disconnect(void* user_data) {
  chip_state_t* chip = (chip_state_t*)user_data;
  chip->read_count = 0;
}

void chip_init() {
  chip_state_t* chip = malloc(sizeof(chip_state_t));
  if (!chip) return;
  chip->current_reg = 0;
  chip->read_count = 0;
  chip->ambient_temp_attr = attr_init_float("ambient_temp", 25.0f);
  chip->object_temp_attr = attr_init_float("object_temp", 25.0f);

  pin_t scl = pin_init("SCL", INPUT_PULLUP);
  pin_t sda = pin_init("SDA", INPUT_PULLUP);

  i2c_config_t config = {
    .scl = scl,
    .sda = sda,
    .address = 0x5A,
    .connect = on_connect,
    .read = on_read,
    .write = on_write,
    .disconnect = on_disconnect,
    .user_data = chip,
  };

  i2c_init(&config);
}
