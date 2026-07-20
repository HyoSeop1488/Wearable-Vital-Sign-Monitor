#include "wokwi-api.h"
#include <stdio.h>
#include <stdlib.h>
#include <math.h>

#define MAX30102_I2C_ADDR 0x57

typedef struct {
  uint8_t current_reg;
  uint8_t read_count;
  uint32_t heart_rate_attr;
  uint32_t spo2_attr;
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

  float hr = attr_read_float(chip->heart_rate_attr);
  float spo2 = attr_read_float(chip->spo2_attr);

  uint16_t value;
  switch (chip->current_reg) {
    case 0x00:
      value = (uint16_t)(hr * 10);
      break;
    case 0x01:
      value = (uint16_t)(spo2 * 10);
      break;
    default:
      value = 0;
  }

  uint8_t low_byte = value & 0xFF;
  uint8_t high_byte = (value >> 8) & 0xFF;

  if (chip->read_count == 0) {
    chip->read_count++;
    return low_byte;
  } else if (chip->read_count == 1) {
    chip->read_count = 0;
    chip->current_reg++;
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
  chip->heart_rate_attr = attr_init_float("heart_rate", 75.0f);
  chip->spo2_attr = attr_init_float("spo2", 98.0f);

  pin_t scl = pin_init("SCL", INPUT_PULLUP);
  pin_t sda = pin_init("SDA", INPUT_PULLUP);

  i2c_config_t config = {
    .scl = scl,
    .sda = sda,
    .address = 0x57,
    .connect = on_connect,
    .read = on_read,
    .write = on_write,
    .disconnect = on_disconnect,
    .user_data = chip,
  };

  i2c_init(&config);
}
