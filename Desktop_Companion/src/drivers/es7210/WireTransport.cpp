#include <Arduino.h>
#include <Wire.h>
#include <esp_err.h>

// C linkage bridges the unmodified C codec logic to Arduino's C++ Wire API.
// Called only from setup, before touch polling begins.
extern "C" esp_err_t companion_es7210_write_reg(uint8_t address, uint8_t reg, uint8_t value) {
  Wire.beginTransmission(address);
  Wire.write(reg);
  Wire.write(value);
  const uint8_t result = Wire.endTransmission();
  if (!result) return ESP_OK;
  Serial.printf("ERROR: ES7210 write failed, register=0x%02x, I2C=%u\n", reg, result);
  return ESP_FAIL;
}

