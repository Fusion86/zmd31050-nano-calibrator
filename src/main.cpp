#include <Arduino.h>
#include <Wire.h>

// Classic 5 V ATmega328P Nano: A4 = SDA, A5 = SCL.
// The manufacturer's address is already 7-bit; do not shift it.
const uint8_t SENSOR_ADDRESS = 0x78;
const uint8_t WORD_COUNT = 32;
const uint8_t ANALOG_OUT_PIN = A0;
// Replace these nominal voltages with multimeter measurements for accuracy.
const float NANO_ADC_REFERENCE_VOLTS = 5.000f;
const float SENSOR_VDDA_VOLTS = 5.000f;
// Voltage-mode OUT limits from the supplied EEPROM dump (addresses 08 and 09).
const uint16_t OUT_DAC_MIN = 0x000;
const uint16_t OUT_DAC_MAX = 0x7FF;
uint16_t firstRead[WORD_COUNT];
uint16_t secondRead[WORD_COUNT];
bool measurementNeedsRestart = false;
bool ramCalibrationActive = false;
uint16_t trialImage[WORD_COUNT];

void printHex(uint16_t value, uint8_t digits)
{
    for (int8_t shift = (digits - 1) * 4; shift >= 0; shift -= 4)
    {
        Serial.print("0123456789ABCDEF"[(value >> shift) & 0x0F]);
    }
}

bool readEepromWord(uint8_t index, uint16_t &value)
{
    // Only READ_EEP0 (0x30..0x3F) and READ_EEP1 (0x40..0x4F)
    // are transmitted. An I2C write transaction here selects data to read;
    // it does not program EEPROM or RAM.
    const uint8_t command = 0x30 + index;
    measurementNeedsRestart = true;
    Wire.beginTransmission(SENSOR_ADDRESS);
    Wire.write(command);
    const uint8_t status = Wire.endTransmission();
    if (status != 0)
    {
        Serial.print(F("ERROR: read command for EEPROM 0x"));
        printHex(index, 2);
        Serial.print(F(" failed; Wire status="));
        Serial.println(status);
        return false;
    }

    // Leave a generous margin over the documented command processing time.
    delay(5);
    const uint8_t received = Wire.requestFrom(SENSOR_ADDRESS, (uint8_t)2);
    if (received != 2 || Wire.available() != 2)
    {
        while (Wire.available())
            Wire.read();
        Serial.print(F("ERROR: expected two bytes at EEPROM 0x"));
        printHex(index, 2);
        Serial.println();
        return false;
    }
    const uint8_t high = Wire.read();
    const uint8_t low = Wire.read();
    value = ((uint16_t)high << 8) | low;
    return true;
}

bool readImage(uint16_t *image)
{
    for (uint8_t index = 0; index < WORD_COUNT; ++index)
    {
        if (!readEepromWord(index, image[index]))
            return false;
    }
    return true;
}

uint16_t calculateSignature(const uint16_t *image)
{
    // Functional Description section 5.4: polynomial signature over 0x00..0x1C.
    // 0x1D stores the signature; 0x1E and 0x1F are free user words.
    uint16_t signature = 0;
    for (uint8_t index = 0; index < 0x1D; ++index)
    {
        signature ^= image[index];
        uint16_t bits = signature & 0xA005;
        uint8_t parity = 0;
        for (uint8_t bit = 0; bit < 16; ++bit)
        {
            parity ^= bits & 1;
            bits >>= 1;
        }
        signature = (uint16_t)((signature << 1) | parity);
    }
    return (uint16_t)~signature;
}

void printField(const __FlashStringHelper *label, uint16_t value)
{
    Serial.print(label);
    Serial.print(F(": "));
    Serial.println(value);
}

void printEepromSummary(const uint16_t *image)
{
    // Decode configuration using Functional Description Rev. 0.82, section 5.3.
    Serial.println(F("BEGIN_EEPROM_SUMMARY"));
    for (uint8_t index = 0; index < 8; ++index)
    {
        Serial.print(F("Pressure coefficient c"));
        Serial.print(index);
        Serial.print(F(" raw: 0x"));
        printHex(image[index], 4);
        Serial.println();
    }
    printField(F("OUT lower limit raw"), image[8]);
    printField(F("OUT upper limit raw"), image[9]);
    const uint16_t cycle = image[0x16];
    const uint16_t sif = image[0x17];
    const uint16_t app = image[0x18];
    const uint16_t afe = image[0x19];
    const uint16_t temp = image[0x1A];
    const uint16_t output = image[0x1B];
    printField(F("Pressure measurements between special measurements"), 1U << ((cycle >> 7) & 7));
    printField(F("OUT source code (0/1=pressure, 2=T1, 3=T2)"), (cycle >> 3) & 3);
    printField(F("Common-mode check enabled"), (cycle >> 2) & 1);
    printField(F("T2 measurement enabled"), (cycle >> 1) & 1);
    printField(F("Startup ROM check enabled"), cycle & 1);
    Serial.println((sif & 1) ? F("Interface: SPI") : F("Interface: I2C"));
    printField(F("Alternative address enabled"), (sif >> 3) & 1);
    Serial.print(F("Alternative address (used only if enabled): 0x"));
    printHex((sif >> 9) & 0x7F, 2);
    Serial.println();
    printField(F("One-wire startup window disabled"), (sif >> 7) & 1);
    printField(F("Digital pressure output enabled"), (sif >> 4) & 1);
    printField(F("Digital T1 output enabled"), (sif >> 5) & 1);
    printField(F("Digital T2 output enabled"), (sif >> 6) & 1);
    printField(F("Bridge polarity reversed"), (app >> 15) & 1);
    printField(F("Sensor connection/parity checks disabled"), (app >> 9) & 1);
    printField(F("Bridge current excitation enabled"), (app >> 8) & 1);
    printField(F("Analog zero compensation enabled"), (app >> 7) & 1);
    printField(F("External clock enabled"), (app >> 4) & 1);
    printField(F("ADC reference code (0=bridge, 1=VDDA)"), (app >> 3) & 1);
    printField(F("Supply regulation enabled"), app & 1);
    const uint8_t resolutionCode = (afe >> 13) & 7;
    printField(F("ADC resolution bits"), resolutionCode >= 6 ? 15 : 9 + resolutionCode);
    printField(F("ADC order"), (afe & 0x1000) ? 2 : 1);
    printField(F("ADC pressure range shift code"), (afe >> 10) & 3);
    printField(F("Analog zero compensation code"), (afe >> 4) & 0x3F);
    printField(F("Pressure PGA gain code"), afe & 0xF);
    printField(F("T1 sensor code (0=internal diode, 1=external diode, 2/3=resistor)"), temp & 3);
    printField(F("T2 sensor code (0=internal diode, 1=external diode, 2=resistor, 3=voltage)"), (temp >> 2) & 3);
    printField(F("OUT mode (0=voltage, 1=current, 2=PWM, 3=disabled)"), output & 3);
    printField(F("IO1 mode (0/1=disabled, 2=PWM, 3=alarm)"), (output >> 2) & 3);
    printField(F("IO2 alarm enabled"), (output >> 4) & 1);
    Serial.println(F("Coefficients are raw encodings, not pressure or temperature units."));
    Serial.println(F("END_EEPROM_SUMMARY"));
}

void backup()
{
    Serial.println(F("Reading EEPROM twice..."));
    if (!readImage(firstRead) || !readImage(secondRead))
    {
        Serial.println(F("BACKUP FAILED. Check wiring, pull-ups and I2C mode."));
        Serial.println(F("Power-cycle the sensor before retrying."));
        return;
    }
    for (uint8_t index = 0; index < WORD_COUNT; ++index)
    {
        if (firstRead[index] != secondRead[index])
        {
            Serial.print(F("BACKUP FAILED: reads differ at EEPROM 0x"));
            printHex(index, 2);
            Serial.println();
            Serial.println(F("Power-cycle the sensor before retrying."));
            return;
        }
    }

    Serial.println(F("BEGIN_ZMD31050_EEPROM_BACKUP"));
    Serial.println(F("address_hex,value_hex"));
    for (uint8_t index = 0; index < WORD_COUNT; ++index)
    {
        printHex(index, 2);
        Serial.print(',');
        printHex(firstRead[index], 4);
        Serial.println();
    }
    Serial.println(F("END_ZMD31050_EEPROM_BACKUP"));
    Serial.println(F("Both reads match."));
    const uint16_t signature = calculateSignature(firstRead);
    Serial.print(F("Calculated signature: "));
    printHex(signature, 4);
    Serial.print(F("; stored signature: "));
    printHex(firstRead[0x1D], 4);
    Serial.println();
    if (signature != firstRead[0x1D])
    {
        Serial.println(F("WARNING: signature invalid; retain this dump, but do not assume it is a valid calibration."));
    }
    else
    {
        Serial.println(F("EEPROM signature valid."));
    }
    printEepromSummary(firstRead);
    Serial.println(F("Save the complete output to a text file."));
    Serial.println(F("Power-cycle the sensor to resume normal measurements."));
}

bool restartMeasurement(bool restoreEeprom = false)
{
    measurementNeedsRestart = true;
    Wire.beginTransmission(SENSOR_ADDRESS);
    const bool useRam = ramCalibrationActive && !restoreEeprom;
    Wire.write((uint8_t)(useRam ? 0x02 : 0x01));
    const uint8_t status = Wire.endTransmission();
    if (status != 0)
    {
        Serial.print(F("ERROR: measurement restart failed; Wire status="));
        Serial.println(status);
        return false;
    }
    // Allow the startup window and initial pressure/temperature conversions.
    delay(250);
    measurementNeedsRestart = false;
    if (restoreEeprom)
        ramCalibrationActive = false;
    return true;
}

bool sendCalibrationCommand(uint8_t command)
{
    measurementNeedsRestart = true;
    Wire.beginTransmission(SENSOR_ADDRESS);
    Wire.write(command);
    const uint8_t status = Wire.endTransmission();
    delay(5);
    if (status != 0)
    {
        Serial.print(F("ERROR: calibration command failed; Wire status="));
        Serial.println(status);
    }
    return status == 0;
}

bool writeAndVerifyRam(uint8_t address, uint16_t value)
{
    Wire.beginTransmission(SENSOR_ADDRESS);
    Wire.write((uint8_t)(0x80 + address));
    Wire.write((uint8_t)(value >> 8));
    Wire.write((uint8_t)value);
    const uint8_t status = Wire.endTransmission();
    delay(5);
    if (status != 0 || !sendCalibrationCommand(0x10 + address))
        return false;
    const uint8_t received = Wire.requestFrom(SENSOR_ADDRESS, (uint8_t)2);
    if (received != 2 || Wire.available() != 2)
    {
        while (Wire.available())
            Wire.read();
        return false;
    }
    const uint8_t high = Wire.read();
    const uint8_t low = Wire.read();
    return (((uint16_t)high << 8) | low) == value;
}

void testRamCalibration()
{
    // Must be the FIRST command after sensor power-on. ACK alone does not
    // prove CM was entered: RAM readback below detects ignored writes.
    if (!sendCalibrationCommand(0x72))
        return;
    ramCalibrationActive = false;
    if (!readImage(firstRead) || !readImage(secondRead))
    {
        restartMeasurement(true);
        return;
    }
    for (uint8_t index = 0; index < WORD_COUNT; ++index)
    {
        if (firstRead[index] != secondRead[index])
        {
            Serial.println(F("ERROR: EEPROM reads differ; calibration aborted."));
            restartMeasurement(true);
            return;
        }
    }
    // These candidate coefficients were calculated for the supplied dump only.
    const uint16_t original[8] = {0xED09, 0xE9EE, 0x004F, 0, 0x0033, 0x00BE, 0x09DC, 0xFA9A};
    const uint16_t corrected[8] = {0xEC6D, 0xE937, 0x004F, 0, 0x0079, 0x0098, 0x0A2E, 0xFA6D};
    bool matches = calculateSignature(firstRead) == firstRead[0x1D] && firstRead[0x1D] == 0x46C8;
    for (uint8_t index = 0; index < 8; ++index)
        matches = matches && firstRead[index] == original[index];
    matches = matches && firstRead[0x19] == 0x9A77 && firstRead[0x17] == 0xF010;
    if (!matches)
    {
        Serial.println(F("ERROR: EEPROM does not match calibration baseline; aborted."));
        restartMeasurement(true);
        return;
    }
    // Restore the complete baseline RAM first so repeated tests cannot compound.
    if (!sendCalibrationCommand(0xC0))
    {
        restartMeasurement(true);
        return;
    }
    for (uint8_t index = 0; index < 8; ++index)
        secondRead[index] = corrected[index];
    secondRead[0x1D] = calculateSignature(secondRead);
    bool verified = true;
    for (uint8_t index = 0; index < 8 && verified; ++index)
        verified = writeAndVerifyRam(index, secondRead[index]);
    if (verified)
        verified = writeAndVerifyRam(0x1D, secondRead[0x1D]);
    if (!verified)
    {
        Serial.println(F("ERROR: RAM verification failed. Power-cycle sensor, then send c FIRST."));
        restartMeasurement(true);
        return;
    }
    ramCalibrationActive = true;
    for (uint8_t index = 0; index < WORD_COUNT; ++index)
        trialImage[index] = secondRead[index];
    if (!restartMeasurement())
    {
        restartMeasurement(true);
        return;
    }
    Serial.println(F("Temporary RAM calibration active: target 0 bar=0.500 V, 1 bar=4.500 V."));
    Serial.println(F("Send d at both reference pressures to verify; r or sensor power-cycle restores EEPROM."));
}

bool readRamImage(uint16_t *image)
{
    for (uint8_t index = 0; index < WORD_COUNT; ++index)
    {
        if (!sendCalibrationCommand(0x10 + index))
            return false;
        const uint8_t received = Wire.requestFrom(SENSOR_ADDRESS, (uint8_t)2);
        if (received != 2 || Wire.available() != 2)
        {
            while (Wire.available())
                Wire.read();
            return false;
        }
        const uint8_t high = Wire.read();
        const uint8_t low = Wire.read();
        image[index] = ((uint16_t)high << 8) | low;
    }
    return true;
}

void saveCalibration()
{
    if (!ramCalibrationActive)
    {
        Serial.println(F("SAVE REFUSED: no active verified c trial. Run c and check endpoints first."));
        return;
    }
    // Check all RAM words against the independent trial snapshot. This catches
    // sensor resets, changed configuration, and backup buffers being reused.
    bool matches = readRamImage(firstRead) &&
                   calculateSignature(trialImage) == trialImage[0x1D];
    for (uint8_t index = 0; index < WORD_COUNT && matches; ++index)
        matches = firstRead[index] == trialImage[index];
    if (!matches)
    {
        Serial.println(F("SAVE REFUSED: RAM differs from verified trial; EEPROM unchanged."));
        ramCalibrationActive = false;
        restartMeasurement(true);
        return;
    }
    Serial.println(F("Saving verified RAM calibration to EEPROM. Keep sensor powered."));
    // C3 copies ALL 32 RAM words including the already verified signature.
    // Datasheet typical time is 400 ms at 2 MHz; allow 1 second margin.
    Wire.beginTransmission(SENSOR_ADDRESS);
    Wire.write((uint8_t)0xC3);
    const uint8_t status = Wire.endTransmission();
    delay(1000);
    // Verify even on a transmission error: a write may have partly completed.
    const bool readBack = readImage(firstRead);
    bool verified = readBack && calculateSignature(firstRead) == firstRead[0x1D];
    for (uint8_t index = 0; index < WORD_COUNT && verified; ++index)
        verified = firstRead[index] == trialImage[index];
    if (!verified)
    {
        Serial.print(F("SAVE FAILED: EEPROM readback not verified; Wire status="));
        Serial.println(status);
        Serial.println(F("EEPROM state uncertain. Retain original backup; do not power-cycle yet."));
        // Keep the verified trial configuration running instead of loading
        // potentially incomplete EEPROM contents.
        restartMeasurement();
        return;
    }
    Serial.println(F("SAVE VERIFIED: all 32 EEPROM words and signature match trial."));
    if (restartMeasurement(true))
        Serial.println(F("Measurement now uses saved EEPROM calibration. Check d at both endpoints."));
}

void testDac(uint16_t code)
{
    // Section 4.5: SET_DAC accepts an 11-bit code, high byte first.
    if (code > 0x07FF)
    {
        Serial.println(F("ERROR: DAC code must be 0x000..0x7FF."));
        return;
    }
    // Even a failed transaction may have interrupted measurement.
    measurementNeedsRestart = true;
    Wire.beginTransmission(SENSOR_ADDRESS);
    Wire.write((uint8_t)0x60);
    Wire.write((uint8_t)(code >> 8));
    Wire.write((uint8_t)code);
    const uint8_t status = Wire.endTransmission();
    if (status != 0)
    {
        Serial.print(F("ERROR: SET_DAC failed; Wire status="));
        Serial.println(status);
        Serial.println(F("Send r to restore measurement."));
        return;
    }
    delay(5);
    Serial.print(F("DAC test code=0x"));
    printHex(code, 3);
    Serial.print(F("; nominal output="));
    Serial.print(code * (100.0f / 2048.0f), 1);
    Serial.println(F("% of sensor VDDA. Measure OUT with a multimeter."));
    Serial.println(F("Send r or d to restore pressure measurement."));
}

void readDigitalPressure()
{
    // EEPROM reads replace the output register and interrupt measurement.
    if (measurementNeedsRestart && !restartMeasurement())
        return;

    // Your EEPROM enables pressure only. It is first if temperatures are enabled.
    // A direct read preserves the running measurement cycle and analog output.
    const uint8_t received = Wire.requestFrom(SENSOR_ADDRESS, (uint8_t)2);
    if (received != 2 || Wire.available() != 2)
    {
        while (Wire.available())
            Wire.read();
        Serial.println(F("ERROR: digital pressure read expected two bytes."));
        return;
    }
    const uint8_t high = Wire.read();
    const uint8_t low = Wire.read();
    const uint16_t value = ((uint16_t)high << 8) | low;
    if (value & 0x8000)
    {
        Serial.print(F("ERROR: sensor diagnostic code 0x"));
        printHex(value, 4);
        Serial.println();
        return;
    }
    Serial.print(F("Digital pressure: 0x"));
    printHex(value, 4);
    Serial.print(F("; count="));
    Serial.print(value);
    Serial.print(F("; full scale="));
    Serial.print(value * (100.0f / 32768.0f), 3);
    Serial.println(F("%"));
    Serial.println(ramCalibrationActive ? F("Calibration: temporary RAM") : F("Calibration: EEPROM"));
    // Discard the first conversion, then average to reduce ADC noise.
    analogRead(ANALOG_OUT_PIN);
    uint32_t adcSum = 0;
    for (uint8_t sample = 0; sample < 16; ++sample)
        adcSum += analogRead(ANALOG_OUT_PIN);
    const float adcCount = adcSum / 16.0f;
    const float measuredVolts = adcCount * (NANO_ADC_REFERENCE_VOLTS / 1024.0f);
    // Voltage output uses an 11-bit DAC, limited by EEPROM words 08/09.
    uint16_t dacCode = value >> 4;
    if (dacCode < OUT_DAC_MIN)
        dacCode = OUT_DAC_MIN;
    if (dacCode > OUT_DAC_MAX)
        dacCode = OUT_DAC_MAX;
    const float expectedVolts = dacCode * (SENSOR_VDDA_VOLTS / 2048.0f);
    Serial.print(F("A0 ADC="));
    Serial.print(adcCount, 2);
    Serial.print(F("; measured V="));
    Serial.print(measuredVolts, 4);
    Serial.print(F("; expected V="));
    Serial.print(expectedVolts, 4);
    Serial.print(F("; measured-expected mV="));
    Serial.println((measuredVolts - expectedVolts) * 1000.0f, 1);
    if (adcCount >= 1023.0f)
        Serial.println(F("A0 ADC is saturated; voltage comparison is unreliable."));
}

void printHelp()
{
    Serial.println(F("Keys: b=EEPROM backup + decode; d=digital pressure + A0 comparison"));
    Serial.println(F("1=25% DAC; 2=50% DAC; 3=75% DAC; r=restore measurement; h=help"));
    Serial.println(F("c=temporary RAM calibration; must be first command after SENSOR power-cycle"));
    Serial.println(F("s=SAVE active verified RAM trial permanently to EEPROM"));
    Serial.println(F("Wire sensor OUT to A0 and share ground; A0 must stay within 0..Nano VCC."));
    Serial.print(F("Nano ADC reference V="));
    Serial.print(NANO_ADC_REFERENCE_VOLTS, 3);
    Serial.print(F("; sensor VDDA V="));
    Serial.println(SENSOR_VDDA_VOLTS, 3);
    Serial.println(F("Set voltage constants to measured values. Comparison assumes saved voltage/pressure configuration."));
}

void setup()
{
    Serial.begin(115200);
    analogReference(DEFAULT);
    pinMode(ANALOG_OUT_PIN, INPUT);
    Wire.begin();
    Wire.setClock(100000);
#if defined(WIRE_HAS_TIMEOUT)
    Wire.setWireTimeout(25000, true);
#endif
    Serial.println(F("ZMD31050 diagnostic tool - s saves the RAM trial to EEPROM"));
    Serial.println(F("Power the sensor ON, wait at least 100 ms, then send b."));
    Serial.println(F("Reading temporarily interrupts normal sensor measurements."));
    Serial.println(F("Send d for digital pressure; after backup it restarts measurement from EEPROM."));
    Serial.println(F("DAC test: 1=25%, 2=50%, 3=75% of sensor VDDA; r=restore measurement."));
    printHelp();
}

void loop()
{
    if (Serial.available())
    {
        const char input = Serial.read();
        if (input == 'b' || input == 'B')
            backup();
        else if (input == 'd' || input == 'D')
            readDigitalPressure();
        else if (input >= '1' && input <= '3')
            testDac((uint16_t)(input - '0') * 0x0200);
        else if (input == 'h' || input == 'H')
            printHelp();
        else if (input == 'c' || input == 'C')
            testRamCalibration();
        else if (input == 's' || input == 'S')
            saveCalibration();
        else if (input == 'r' || input == 'R')
        {
            if (restartMeasurement(true))
                Serial.println(F("Pressure measurement restored from EEPROM."));
        }
    }
}
