#include "TCA9554PWR.h"

TCA9554PWR::TCA9554PWR(uint8_t address) : _address(address) {
}

void TCA9554PWR::begin() {
    Wire.beginTransmission(_address);
    Wire.endTransmission();
}

uint8_t TCA9554PWR::readRegister(uint8_t reg) {
    Wire.beginTransmission(_address);
    Wire.write(reg);
    if (Wire.endTransmission(false) != 0) {
        Serial.println("TCA9554 I2C read failed!");
        return 0;
    }
    Wire.requestFrom(_address, (uint8_t)1);
    return Wire.read();
}

bool TCA9554PWR::writeRegister(uint8_t reg, uint8_t data) {
    Wire.beginTransmission(_address);
    Wire.write(reg);
    Wire.write(data);
    return (Wire.endTransmission() == 0);
}

void TCA9554PWR::pinMode(uint8_t pin, uint8_t state) {
    if (pin < 1 || pin > 8) return;
    uint8_t current = readRegister(REG_CONFIG);
    uint8_t mask = (1 << (pin - 1));
    if (state == 1) {
        current |= mask; // Input
    } else {
        current &= ~mask; // Output
    }
    writeRegister(REG_CONFIG, current);
}

void TCA9554PWR::modeAll(uint8_t pinState) {
    writeRegister(REG_CONFIG, pinState);
}

uint8_t TCA9554PWR::readPin(uint8_t pin) {
    if (pin < 1 || pin > 8) return 0;
    uint8_t inputs = readRegister(REG_INPUT);
    return (inputs >> (pin - 1)) & 0x01;
}

uint8_t TCA9554PWR::readAll() {
    return readRegister(REG_INPUT);
}

void TCA9554PWR::digitalWrite(uint8_t pin, uint8_t state) {
    if (pin < 1 || pin > 8) return;
    uint8_t current = readRegister(REG_OUTPUT);
    uint8_t mask = (1 << (pin - 1));
    if (state == 1) {
        current |= mask;
    } else {
        current &= ~mask;
    }
    writeRegister(REG_OUTPUT, current);
}

void TCA9554PWR::writeAll(uint8_t pinState) {
    writeRegister(REG_OUTPUT, pinState);
}
