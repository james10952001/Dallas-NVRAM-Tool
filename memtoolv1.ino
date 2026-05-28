/*
 * Universal NVRAM tool
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.
 * See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see <https://www.gnu.org/licenses/>.
 */

#include <Arduino.h>
#include <EEPROM.h>

const int EEPROM_CONFIG_ADDR = 0;
const int EEPROM_CONFIG_SIZE = 32; // Reserve a generous 32 bytes for future-proofing

// ============================================================
// DEVICE TYPES
// ============================================================

enum DeviceType {
  DEV_GENERIC_24 = 0,
  DEV_GENERIC_28 = 1,
  DEV_GENERIC_32 = 2,
  DEV_DS1742 = 3,
  DEV_DS1644 = 4,
  DEV_KM684000 = 5,
  DEV_MAX // Used strictly for EEPROM boundary validation
};

DeviceType g_device = DEV_DS1742;

// ============================================================
// DYNAMIC PROFILES
// ============================================================

struct PinMap {
  int8_t addr[19]; // Increased from 15 to 19 (A0 - A18) for 512KB chips
  int8_t data[8];  // DQ0 - DQ7
  int8_t ce;
  int8_t oe;
  int8_t we;
  int8_t ce2;      // SECOND Chip Enable (Active HIGH)
};

struct RTCProfile {
  bool hasRTC;
  uint32_t baseAddress; 
  bool hasCentury;     // Does Control register contain Century data?
  bool hasBatteryFlag; // Does Day register contain a Battery Flag?
};

struct DeviceInfo {
  const char* name;
  uint32_t sizeBytes;
  uint32_t protectedStart; 
  RTCProfile rtc;
  PinMap pins;
};

// Global active pin map for fast low-level access
PinMap g_activePins;

// ============================================================
// DEVICE INFO
// ============================================================

// The arduino to 32 pin socket mapping is as follows:
// D40	1
// D38	2
// D36	3
// D34	4
// D29	5
// D28	6
// D27	7
// D26	8
// D25	9
// D24	10
// D23	11
// D22	12
// D44	13
// D45	14
// D46	15
// Gnd	16
// D47	17
// D48	18
// D49	19
// D50	20
// D51	21
// D41	22
// D32	23
// D42	24
// D33	25
// D31	26
// D30	27
// D35	28
// D43	29
// D39	30
// D37	31
// Vcc	32

// Devices smaller than 32 pins are inserted at the lower end of the socket resulting in 
// their pin numbers being offset by 4 for 24 pin devices and 2 for 28 pin devices.


DeviceInfo getDeviceInfo() {
  switch (g_device) {

    case DEV_GENERIC_24:
      return {
        "Generic SRAM/ROM (24 pin)",
        2048,
        0xFFFFFFFF,
        { false, 0, false, false },
        { 
          // A0-A10
          {22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, -1, -1, -1, -1, -1, -1, -1, -1}, 
          {44, 45, 46, 47, 48, 49, 50, 51}, 
          41, 42, 33, -1 
        }
      };

    case DEV_GENERIC_28:
      return {
        "Generic SRAM/ROM (28 pin)",
        8192,
        0xFFFFFFFF,
        { false, 0, false, false },
        { 
          // A0-A12
          {22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, -1, -1, -1, -1, -1, -1}, 
          {44, 45, 46, 47, 48, 49, 50, 51}, 
          41, 42, 43, 35  // D35 drives CE2 HIGH
        }
      };

    case DEV_GENERIC_32:
      return {
        "Generic SRAM/ROM (32 pin)",
        32768,
        0xFFFFFFFF,
        { false, 0, false, false },
        { 
          // A0-A14
          {22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, -1, -1, -1, -1}, 
          {44, 45, 46, 47, 48, 49, 50, 51}, 
          41, 42, 43, -1 
        }
      };

    case DEV_DS1742:
      return {
        "Dallas DS1742",
        2048,
        0x7F8,
        { true, 0x7F8, true, true },
        { 
          // A0-A10
          {22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, -1, -1, -1, -1, -1, -1, -1, -1}, 
          {44, 45, 46, 47, 48, 49, 50, 51}, 
          41, 42, 33, -1 
        }
      };

    case DEV_DS1644:
      return {
        "Dallas DS1644",
        32768,
        0x7FF8,
        { true, 0x7FF8, false, false },
        { 
          // A0-A14
          {22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, -1, -1, -1, -1}, 
          {44, 45, 46, 47, 48, 49, 50, 51}, 
          41, 42, 43, -1 
        }
      };

    case DEV_KM684000:
      return {
        "Samsung KM684000",
        524288, // 512KB Capacity
        0xFFFFFFFF,
        { false, 0, false, false },
        { 
          // A0-A18 sequential magic!
          {22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, 33, 34, 35, 36, 37, 38, 39, 40}, 
          {44, 45, 46, 47, 48, 49, 50, 51}, 
          41, 42, 43, -1 
        }
      };
  }

  // Default fallback (Safe generic 2KB state)
  return {
    "Unknown",
    2048,
    0xFFFFFFFF,
    { false, 0, false, false },
    { 
      {22, 23, 24, 25, 26, 27, 28, 29, 30, 31, 32, -1, -1, -1, -1, -1, -1, -1, -1}, 
      {44, 45, 46, 47, 48, 49, 50, 51}, 
      41, 42, 33, -1 
    }
  };
}

// ============================================================
// RTC BIT DEFINITIONS
// ============================================================

const uint8_t BIT_W   = 0x80;
const uint8_t BIT_R   = 0x40;
const uint8_t BIT_OSC = 0x80;
const uint8_t BIT_FT  = 0x40;
const uint8_t BIT_BF  = 0x80;

// ============================================================
// GLOBALS
// ============================================================

uint8_t g_century = 0;

// ============================================================
// UTILITY FUNCTIONS
// ============================================================

void printHexAddress(uint32_t address) {
  if (address <= 0xFFFF) {
    char buf[5];
    sprintf(buf, "%04lX", address);
    Serial.print(buf);
  } else {
    char buf[9];
    sprintf(buf, "%08lX", address);
    Serial.print(buf);
  }
}

void printHexByte(uint8_t value) {
  if (value < 16) {
    Serial.print('0');
  }
  Serial.print(value, HEX);
}

void print2Digits(uint8_t value) {
  if (value < 10) {
    Serial.print('0');
  }
  Serial.print(value);
}

// ============================================================
// RTC REGISTER HELPERS
// ============================================================

uint32_t regControl() { return getDeviceInfo().rtc.baseAddress + 0; }
uint32_t regSeconds() { return getDeviceInfo().rtc.baseAddress + 1; }
uint32_t regMinutes() { return getDeviceInfo().rtc.baseAddress + 2; }
uint32_t regHours()   { return getDeviceInfo().rtc.baseAddress + 3; }
uint32_t regDay()     { return getDeviceInfo().rtc.baseAddress + 4; }
uint32_t regDate()    { return getDeviceInfo().rtc.baseAddress + 5; }
uint32_t regMonth()   { return getDeviceInfo().rtc.baseAddress + 6; }
uint32_t regYear()    { return getDeviceInfo().rtc.baseAddress + 7; }

// ============================================================
// LOW LEVEL BUS FUNCTIONS
// ============================================================

void applyPinConfiguration() {
  // 1. Reset standard socket pins to INPUT (High-Z) to prevent shorts
  for(uint8_t i = 22; i <= 53; i++) {
     pinMode(i, INPUT);
  }

  // 2. Load the active pin map
  g_activePins = getDeviceInfo().pins;

// 3. Configure Address pins
  for (uint8_t i = 0; i < 19; i++) { // <-- Change to 19
    if (g_activePins.addr[i] != -1) {
      pinMode(g_activePins.addr[i], OUTPUT);
      digitalWrite(g_activePins.addr[i], LOW);
    }
  }

  // 4. Configure Control pins
  pinMode(g_activePins.ce, OUTPUT);
  pinMode(g_activePins.oe, OUTPUT);
  pinMode(g_activePins.we, OUTPUT);
  
  if (g_activePins.ce2 != -1) {
    pinMode(g_activePins.ce2, OUTPUT);
    digitalWrite(g_activePins.ce2, HIGH); // CE2 is active HIGH
  }

  // 5. Secure the bus
  busIdle();
  setDataBusInput();
}

void busIdle() {
  digitalWrite(g_activePins.ce, HIGH);
  digitalWrite(g_activePins.oe, HIGH);
  digitalWrite(g_activePins.we, HIGH);
}

void setAddress(uint32_t address) {
  for (uint8_t i = 0; i < 19; i++) { // <-- Change to 19
    if (g_activePins.addr[i] != -1) {
      digitalWrite(g_activePins.addr[i], (address >> i) & 1);
    }
  }
}

void setDataBusInput() {
  for (uint8_t i = 0; i < 8; i++) {
    pinMode(g_activePins.data[i], INPUT);
  }
}

void setDataBusOutput() {
  for (uint8_t i = 0; i < 8; i++) {
    pinMode(g_activePins.data[i], OUTPUT);
  }
}

void writeDataBus(uint8_t value) {
  for (uint8_t i = 0; i < 8; i++) {
    digitalWrite(g_activePins.data[i], (value >> i) & 1);
  }
}

uint8_t readDataBus() {
  uint8_t value = 0;
  for (uint8_t i = 0; i < 8; i++) {
    if (digitalRead(g_activePins.data[i])) {
      value |= (1 << i);
    }
  }
  return value;
}

// ============================================================
// RAW MEMORY ACCESS
// ============================================================

uint8_t readByteRaw(uint32_t address) {
  busIdle();
  setAddress(address);
  setDataBusInput();
  delayMicroseconds(1);
  digitalWrite(g_activePins.ce, LOW);
  digitalWrite(g_activePins.oe, LOW);
  delayMicroseconds(2);
  uint8_t value = readDataBus();
  busIdle();
  return value;
}

void writeByteRaw(uint32_t address, uint8_t value) {
  busIdle();
  setAddress(address);
  digitalWrite(g_activePins.oe, HIGH);
  setDataBusOutput();
  writeDataBus(value);
  delayMicroseconds(1);
  digitalWrite(g_activePins.ce, LOW);
  delayMicroseconds(1);
  digitalWrite(g_activePins.we, LOW);
  delayMicroseconds(2);
  digitalWrite(g_activePins.we, HIGH);
  busIdle();
  setDataBusInput();
}

// ============================================================
// RTC HELPERS
// ============================================================

bool requireRTC() {
  if (!getDeviceInfo().rtc.hasRTC) {
    Serial.println(F("Selected device has no RTC."));
    return false;
  }
  return true;
}

uint8_t bcdToDec(uint8_t value) {
  return ((value >> 4) * 10) + (value & 0x0F);
}

uint8_t decToBcd(uint8_t value) {
  return ((value / 10) << 4) | (value % 10);
}

void rtcBeginRead() {
  DeviceInfo info = getDeviceInfo();
  if (!info.rtc.hasRTC) return;

  uint8_t control = readByteRaw(regControl());
  
  if (info.rtc.hasCentury) {
    g_century = control & 0x3F;
    writeByteRaw(regControl(), BIT_R | g_century);
  } else {
    writeByteRaw(regControl(), BIT_R);
  }
}

void rtcEndRead() {
  DeviceInfo info = getDeviceInfo();
  if (!info.rtc.hasRTC) return;

  if (info.rtc.hasCentury) {
    writeByteRaw(regControl(), g_century);
  } else {
    writeByteRaw(regControl(), 0);
  }
  delayMicroseconds(600);
}

void rtcBeginWrite() {
  DeviceInfo info = getDeviceInfo();
  if (!info.rtc.hasRTC) return;

  uint8_t control = readByteRaw(regControl());
  
  if (info.rtc.hasCentury) {
    g_century = control & 0x3F;
    writeByteRaw(regControl(), BIT_W | g_century);
  } else {
    writeByteRaw(regControl(), BIT_W);
  }
}

void rtcEndWrite() {
  DeviceInfo info = getDeviceInfo();
  if (!info.rtc.hasRTC) return;

  if (info.rtc.hasCentury) {
    writeByteRaw(regControl(), g_century);
  } else {
    writeByteRaw(regControl(), 0);
  }
}

// ============================================================
// SERIAL HELPERS
// ============================================================

String readLine() {
  String s = "";
  while (true) {
    while (!Serial.available()) {}
    char c = Serial.read();
    if (c == '\r' || c == '\n') {
      Serial.println();
      break;
    }
    if ((c == 8 || c == 127) && s.length() > 0) {
      s.remove(s.length() - 1);
      Serial.print(F("\b \b"));
      continue;
    }
    Serial.print(c);
    s += c;
  }
  s.trim();
  s.toUpperCase();
  return s;
}

bool parseHex32(String s, uint32_t &value) {
  char *endptr;
  unsigned long v = strtoul(s.c_str(), &endptr, 16);
  if (*endptr != '\0') return false;
  value = v;
  return true;
}

bool parseHex8(String s, uint8_t &value) {
  char *endptr;
  unsigned long v = strtoul(s.c_str(), &endptr, 16);
  if (*endptr != '\0') return false;
  if (v > 0xFF) return false;
  value = (uint8_t)v;
  return true;
}

// ============================================================
// INTEL HEX HELPERS
// ============================================================

uint8_t intelHexChecksum(uint8_t count, uint16_t address, uint8_t type, const uint8_t* data) {
  uint8_t sum = 0;
  sum += count;
  sum += (address >> 8) & 0xFF;
  sum += address & 0xFF;
  sum += type;
  for (uint8_t i = 0; i < count; i++) {
    sum += data[i];
  }
  return (uint8_t)(0 - sum);
}

uint8_t hexDigit(char c) {
  if (c >= '0' && c <= '9') return c - '0';
  if (c >= 'A' && c <= 'F') return c - 'A' + 10;
  if (c >= 'a' && c <= 'f') return c - 'a' + 10;
  return 0;
}

uint8_t parseHexByte(const String &s, int index) {
  return (hexDigit(s[index]) << 4) | hexDigit(s[index + 1]);
}

// ============================================================
// DEVICE SELECTION
// ============================================================

void handleSelectDevice() {
  Serial.println();
  Serial.println(F("SELECT DEVICE"));
  Serial.println();
  Serial.println(F("A. Generic SRAM/ROM (24 pin)"));
  Serial.println(F("B. Generic SRAM/ROM (28 pin)"));
  Serial.println(F("C. Generic SRAM/ROM (32 pin)"));
  Serial.println(F("D. Dallas DS1742"));
  Serial.println(F("E. Dallas DS1644"));
  Serial.println(F("F. Samsung KM684000"));
  Serial.println();
  Serial.print(F("Selection: "));
  
  String s = readLine();
  switch (s.charAt(0)) {
    case 'A': g_device = DEV_GENERIC_24; break;
    case 'B': g_device = DEV_GENERIC_28; break;
    case 'C': g_device = DEV_GENERIC_32; break;
    case 'D': g_device = DEV_DS1742; break;
    case 'E': g_device = DEV_DS1644; break;
    case 'F': g_device = DEV_KM684000; break;
    default:
      Serial.println(F("INVALID"));
      return;
  }
  
  // Wipe and save the new selection to EEPROM
  saveDeviceToEEPROM();
  
  // Reconfigure the hardware for the newly selected device
  applyPinConfiguration();

  Serial.println();
  Serial.print(F("Selected: "));
  Serial.println(getDeviceInfo().name);
}


// ============================================================
// READ BYTE
// ============================================================

void handleReadByte() {
  Serial.println();
  Serial.println(F("READ BYTE"));
  Serial.print(F("Address HEX: "));
  
  uint32_t address;
  if (!parseHex32(readLine(), address)) {
    Serial.println(F("INVALID"));
    return;
  }
  
  uint8_t value = readByteRaw(address);
  Serial.println();
  Serial.print(F("HEX: 0x"));
  printHexByte(value);
  Serial.println();
  Serial.print(F("DEC: "));
  Serial.println(value);
  Serial.print(F("BIN: "));
  
  for (int i = 7; i >= 0; i--) {
    Serial.print((value >> i) & 1);
    if (i == 4) Serial.print(' ');
  }
  Serial.println();
}

// ============================================================
// WRITE BYTE
// ============================================================

void handleWriteByte() {
  Serial.println();
  Serial.println(F("WRITE BYTE"));
  Serial.print(F("Address HEX: "));
  
  uint32_t address;
  if (!parseHex32(readLine(), address)) {
    Serial.println(F("INVALID"));
    return;
  }
  
  Serial.print(F("Data HEX: "));
  uint8_t value;
  if (!parseHex8(readLine(), value)) {
    Serial.println(F("INVALID"));
    return;
  }
  
  writeByteRaw(address, value);
  uint8_t verify = readByteRaw(address);
  Serial.print(F("Verify: 0x"));
  printHexByte(verify);
  Serial.println();
}

// ============================================================
// DUMP MEMORY RAW
// ============================================================

void handleDumpMemory() {
  DeviceInfo info = getDeviceInfo();
  uint32_t startAddr = 0;
  uint32_t endAddr = info.sizeBytes - 1;
  
  Serial.println();
  Serial.println(F("DUMP MEMORY"));
  Serial.println();
  Serial.print(F("Start Address HEX (default 0000): "));
  
  String s = readLine();
  if (s.length() > 0) {
    if (!parseHex32(s, startAddr)) {
      Serial.println(F("INVALID"));
      return;
    }
  }
  
  Serial.print(F("End Address HEX (default "));
  printHexAddress(endAddr);
  Serial.print(F("): "));
  
  s = readLine();
  if (s.length() > 0) {
    if (!parseHex32(s, endAddr)) {
      Serial.println(F("INVALID"));
      return;
    }
  }
  
  if (startAddr >= info.sizeBytes || endAddr >= info.sizeBytes || endAddr < startAddr) {
    Serial.println(F("INVALID RANGE"));
    return;
  }
  
  Serial.println();
  Serial.println(F("DUMP"));
  Serial.println();
  
  for (uint32_t addr = startAddr; addr <= endAddr; addr += 16) {
    printHexAddress(addr);
    Serial.print(F(": "));
    
    for (uint8_t i = 0; i < 16; i++) {
      uint32_t current = addr + i;
      if (current > endAddr) {
        Serial.print(F("   "));
        continue;
      }
      printHexByte(readByteRaw(current));
      Serial.print(' ');
    }
    
    Serial.print(F(" "));
    for (uint8_t i = 0; i < 16; i++) {
      uint32_t current = addr + i;
      if (current > endAddr) break;
      uint8_t value = readByteRaw(current);
      if (value >= 32 && value <= 126) {
        Serial.print((char)value);
      } else {
        Serial.print('.');
      }
    }
    Serial.println();
    if (addr + 16 < addr) break;
  }
  Serial.println();
}

// ============================================================
// DUMP TO INTEL HEX
// ============================================================

void handleDumpHex() {
  DeviceInfo info = getDeviceInfo();
  uint32_t endAddr = info.sizeBytes - 1;
  
  Serial.println();
  Serial.println(F("DUMP TO INTEL HEX"));
  Serial.println();
  Serial.print(F("End Address HEX (default "));
  printHexAddress(endAddr);
  Serial.print(F("): "));
  
  String s = readLine();
  if (s.length() > 0) {
    if (!parseHex32(s, endAddr)) {
      Serial.println(F("INVALID"));
      return;
    }
  }
  
  if (endAddr >= info.sizeBytes) {
    Serial.println(F("INVALID RANGE"));
    return;
  }
  
  Serial.println();
  Serial.println(F("BEGIN INTEL HEX"));
  Serial.println();
  
  uint8_t data[16];
  uint32_t addr = 0;
  
  while (addr <= endAddr) {
    uint8_t count = 16;
    if ((addr + count - 1) > endAddr) {
      count = (endAddr - addr) + 1;
    }
    
    for (uint8_t i = 0; i < count; i++) {
      data[i] = readByteRaw(addr + i);
    }
    
    uint16_t lineAddr = addr & 0xFFFF;
    uint8_t checksum = intelHexChecksum(count, lineAddr, 0x00, data);
    
    Serial.print(':');
    printHexByte(count);
    char buf[5];
    sprintf(buf, "%04X", lineAddr);
    Serial.print(buf);
    Serial.print(F("00"));
    
    for (uint8_t i = 0; i < count; i++) {
      printHexByte(data[i]);
    }
    printHexByte(checksum);
    Serial.println();
    addr += count;
  }
  
  Serial.println(F(":00000001FF"));
  Serial.println();
  Serial.println(F("END INTEL HEX"));
}

// ============================================================
// PROGRAM FROM INTEL HEX
// ============================================================

void handleProgramHex() {
  DeviceInfo info = getDeviceInfo();
  Serial.println();
  Serial.println(F("PROGRAM FROM INTEL HEX"));
  Serial.println();
  Serial.println(F("Paste Intel HEX records."));
  Serial.println(F("Send EOF record (:00000001FF) to finish."));
  Serial.println();
  
  Serial.setTimeout(2000);
  while (Serial.available()) {
    Serial.read();
  }
  
  uint32_t lines = 0;
  uint32_t bytesWritten = 0;
  uint32_t bytesSkipped = 0;
  
  while (true) {
    String line = Serial.readStringUntil('\n');
    line.trim();
    line.toUpperCase();
    
    if (line.length() == 0) continue;
    if (line.charAt(0) != ':') {
      Serial.println(F("INVALID RECORD"));
      continue;
    }
    
    if (line == ":00000001FF") {
      Serial.println();
      Serial.println(F("IMPORT COMPLETE"));
      Serial.print(F("Lines: "));
      Serial.println(lines);
      Serial.print(F("Bytes Written: "));
      Serial.println(bytesWritten);
      Serial.print(F("Bytes Skipped: "));
      Serial.println(bytesSkipped);
      return;
    }
    
    if (line.length() < 11) {
      Serial.println(F("SHORT RECORD"));
      continue;
    }
    
    uint8_t count = parseHexByte(line, 1);
    uint16_t address = ((uint16_t)parseHexByte(line, 3) << 8) | parseHexByte(line, 5);
    uint8_t type = parseHexByte(line, 7);
    
    if (type != 0x00) continue;
    
    uint16_t expectedLength = 11 + (count * 2);
    if (line.length() < expectedLength) {
      Serial.println(F("TRUNCATED RECORD"));
      continue;
    }
    
    uint8_t sum = 0;
    for (uint16_t i = 1; i < (9 + count * 2); i += 2) {
      sum += parseHexByte(line, i);
    }
    uint8_t checksum = parseHexByte(line, 9 + (count * 2));
    sum += checksum;
    
    if (sum != 0) {
      Serial.println(F("CHECKSUM ERROR"));
      continue;
    }
    
    for (uint8_t i = 0; i < count; i++) {
      uint32_t addr = address + i;
      if (addr >= info.sizeBytes) continue;
      
      if (info.rtc.hasRTC && addr >= info.protectedStart) {
        bytesSkipped++;
        continue;
      }
      
      uint8_t value = parseHexByte(line, 9 + (i * 2));
      writeByteRaw(addr, value);
      bytesWritten++;
    }
    
    lines++;
    if ((lines & 0x3F) == 0) {
      Serial.print('.');
    }
  }
}

// ============================================================
// CLEAR MEMORY
// ============================================================

void handleClearMemory() {
  DeviceInfo info = getDeviceInfo();
  
  Serial.println();
  Serial.print(F("Clear memory on "));
  Serial.print(info.name);
  Serial.println(F("?"));
  
  if (info.rtc.hasRTC) {
    Serial.println(F("RTC/status registers will be preserved."));
  }
  
  Serial.println(F("Type YES to continue."));
  Serial.print(F("> "));
  String confirm = readLine();
  
  if (confirm != "YES") {
    Serial.println(F("Cancelled."));
    return;
  }
  
  Serial.println();
  Serial.println(F("Clearing..."));
  
  int dotCount = 0; // Track how many dots we've printed
  
  for (uint32_t addr = 0; addr < info.sizeBytes; addr++) {
    if (info.rtc.hasRTC && addr >= info.protectedStart) {
      continue;
    }
    
    writeByteRaw(addr, 0xFF);
    
    // Print a dot every 256 bytes
    if ((addr & 0xFF) == 0) {
      Serial.print('.');
      dotCount++;
      
      // Wrap to a new line every 40 dots
      if (dotCount >= 40) {
        Serial.println();
        dotCount = 0;
      }
    }
  }
  
  // Only print the final newline if we didn't just wrap the line
  if (dotCount > 0) {
    Serial.println();
  }
  Serial.println(F("Done."));
}

// ============================================================
// RTC STATUS
// ============================================================

void handleRTCStatus() {
  if (!requireRTC()) return;
  Serial.println();
  Serial.println(F("RTC STATUS"));
  Serial.println(F("Press ENTER to stop."));
  
  unsigned long lastUpdate = 0;
  while (true) {
    unsigned long now = millis();
    if (now - lastUpdate >= 500) {
      rtcBeginRead();
      uint8_t control = readByteRaw(regControl());
      uint8_t seconds = readByteRaw(regSeconds());
      uint8_t minutes = readByteRaw(regMinutes());
      uint8_t hours   = readByteRaw(regHours());
      uint8_t day     = readByteRaw(regDay());
      uint8_t date    = readByteRaw(regDate());
      uint8_t month   = readByteRaw(regMonth());
      uint8_t year    = readByteRaw(regYear());
      rtcEndRead();
      
      DeviceInfo info = getDeviceInfo();
      bool oscStopped = seconds & BIT_OSC;
      bool freqTest   = day & BIT_FT;
      bool battery    = false;
      
      if (info.rtc.hasBatteryFlag) {
        battery = day & BIT_BF;
      }
      
      seconds &= 0x7F;
      day &= 0x07;
      
      uint16_t fullYear = 0;
      if (info.rtc.hasCentury) {
        fullYear = ((control & 0x3F) * 100) + bcdToDec(year);
      } else {
        fullYear = 2000 + bcdToDec(year); // Safe modern default for DS1644
      }
      
      Serial.write(27);
      Serial.print(F("[2J"));
      Serial.write(27);
      Serial.print(F("[H"));
      
      Serial.println(F("RTC STATUS"));
      Serial.println();
      Serial.print(F("Time: "));
      print2Digits(bcdToDec(hours));
      Serial.print(':');
      print2Digits(bcdToDec(minutes));
      Serial.print(':');
      print2Digits(bcdToDec(seconds));
      Serial.println();
      
      Serial.print(F("Date: "));
      print2Digits(bcdToDec(month));
      Serial.print('/');
      print2Digits(bcdToDec(date));
      Serial.print('/');
      Serial.println(fullYear);
      
      Serial.print(F("Day: "));
      Serial.println(day);
      Serial.print(F("Oscillator: "));
      Serial.println(oscStopped ? F("STOPPED") : F("RUNNING"));
      Serial.print(F("Frequency Test: "));
      Serial.println(freqTest ? F("ON") : F("OFF"));
      
      if (info.rtc.hasBatteryFlag) {
        Serial.print(F("Battery Level: "));
        Serial.println(battery ? F("OK") : F("LOW"));
      } else {
        Serial.println(F("Battery Level: N/A (Not supported by chip)"));
      }
      
      Serial.println();
      Serial.println(F("Press ENTER to stop."));
      lastUpdate = now;
    }
    
    if (Serial.available()) {
      char c = Serial.read();
      if (c == '\r' || c == '\n') break;
    }
  }
  Serial.println();
}

// ============================================================
// SET TIME
// ============================================================

void handleSetTime() {
  if (!requireRTC()) return;
  Serial.println();
  Serial.println(F("SET TIME"));
  Serial.println(F("Format HH:MM:SS"));
  Serial.print(F("Time: "));
  
  String s = readLine();
  int h, m, sec;
  if (sscanf(s.c_str(), "%d:%d:%d", &h, &m, &sec) != 3) {
    Serial.println(F("INVALID"));
    return;
  }
  
  rtcBeginWrite();
  writeByteRaw(regSeconds(), decToBcd(sec) & 0x7F);
  writeByteRaw(regMinutes(), decToBcd(m));
  writeByteRaw(regHours(), decToBcd(h));
  rtcEndWrite();
  
  Serial.println(F("TIME SET"));
}

// ============================================================
// SET DATE
// ============================================================

void handleSetDate() {
  if (!requireRTC()) return;
  Serial.println();
  Serial.println(F("SET DATE"));
  Serial.println(F("Format MM/DD/YYYY"));
  Serial.print(F("Date: "));
  
  String s = readLine();
  int mo, da, yr;
  if (sscanf(s.c_str(), "%d/%d/%d", &mo, &da, &yr) != 3) {
    Serial.println(F("INVALID"));
    return;
  }
  
  uint8_t century = yr / 100;
  uint8_t year = yr % 100;
  
  rtcBeginWrite();
  DeviceInfo info = getDeviceInfo();
  if (info.rtc.hasCentury) {
    g_century = century & 0x3F;
  }
  
  writeByteRaw(regDate(), decToBcd(da));
  writeByteRaw(regMonth(), decToBcd(mo));
  writeByteRaw(regYear(), decToBcd(year));
  rtcEndWrite();
  
  Serial.println(F("DATE SET"));
}

// ============================================================
// SET DAY
// ============================================================

void handleSetDay() {
  if (!requireRTC()) return;
  Serial.println();
  Serial.println(F("SET DAY"));
  Serial.println(F("1=Sun 2=Mon ... 7=Sat"));
  Serial.print(F("Day: "));
  
  int day = readLine().toInt();
  if (day < 1 || day > 7) {
    Serial.println(F("INVALID"));
    return;
  }
  
  rtcBeginWrite();
  uint8_t dayReg = readByteRaw(regDay());
  dayReg &= 0xF8;
  dayReg |= (day & 0x07);
  writeByteRaw(regDay(), dayReg);
  rtcEndWrite();
  
  Serial.println(F("DAY SET"));
}

// ============================================================
// RTC FREQUENCY TEST
// ============================================================

void handleFrequencyTest() {
  if (!requireRTC()) return;
  Serial.println();
  Serial.println(F("RTC FREQUENCY TEST"));
  Serial.println(F("Press ENTER to stop."));
  Serial.println();
  
  rtcBeginWrite();
  uint8_t dayReg = readByteRaw(regDay());
  dayReg |= BIT_FT;
  writeByteRaw(regDay(), dayReg);
  rtcEndWrite();
  busIdle();
  
  setAddress(regSeconds());
  setDataBusInput();
  
  digitalWrite(g_activePins.we, HIGH);
  digitalWrite(g_activePins.ce, LOW);
  digitalWrite(g_activePins.oe, LOW);
  
  bool lastState = digitalRead(g_activePins.data[0]);
  unsigned long edgeCount = 0;
  unsigned long startTime = millis();
  unsigned long lastPrint = 0;
  
  while (true) {
    bool currentState = digitalRead(g_activePins.data[0]);
    if (currentState != lastState) {
      edgeCount++;
      lastState = currentState;
    }
    
    unsigned long now = millis();
    if (now - lastPrint >= 1000) {
      float elapsed = (now - startTime) / 1000.0;
      float frequency = (edgeCount / 2.0) / elapsed;
      Serial.print(F("\rFrequency: "));
      Serial.print(frequency, 2);
      Serial.print(F(" Hz   "));
      lastPrint = now;
    }
    
    if (Serial.available()) {
      char c = Serial.read();
      if (c == '\r' || c == '\n') break;
    }
  }
  
  Serial.println();
  Serial.println();
  
  busIdle();
  rtcBeginWrite();
  dayReg = readByteRaw(regDay());
  dayReg &= ~BIT_FT;
  writeByteRaw(regDay(), dayReg);
  rtcEndWrite();
  
  Serial.println(F("Frequency test disabled"));
}

// ============================================================
// MENU
// ============================================================

void showMenu() {
  DeviceInfo info = getDeviceInfo();
  Serial.println();
  Serial.println(F("========== MEMORY TOOL =========="));
  Serial.print(F("Device: "));
  Serial.println(info.name);
  Serial.println();
  Serial.println(F("S - Select Device"));
  Serial.println(F("R - Read Byte"));
  Serial.println(F("W - Write Byte"));
  Serial.println(F("M - Dump Memory"));
  Serial.println(F("H - Dump Intel HEX"));
  Serial.println(F("P - Program Intel HEX"));
  Serial.println(F("C - Clear Memory"));
  
  if (info.rtc.hasRTC) {
    Serial.println(F("T - RTC Status"));
    Serial.println(F("I - Set Time"));
    Serial.println(F("A - Set Date"));
    Serial.println(F("Y - Set Day"));
    Serial.println(F("F - Frequency Test"));
  }
  
  Serial.println();
  Serial.print(F("Select Option: "));
}

// ============================================================
// NONVOLATILE STORAGE
// ============================================================

void saveDeviceToEEPROM() {
  // 1. Wipe the entire 32-byte reserved space to 0xFF 
  // (Using EEPROM.update saves write cycles if it's already 0xFF)
  for (int i = 0; i < EEPROM_CONFIG_SIZE; i++) {
    EEPROM.update(EEPROM_CONFIG_ADDR + i, 0xFF);
  }
  
  // 2. Cast and save the selected device as a 32-bit unsigned integer
  uint32_t deviceVal = (uint32_t)g_device;
  EEPROM.put(EEPROM_CONFIG_ADDR, deviceVal);
}

void loadDeviceFromEEPROM() {
  uint32_t deviceVal;
  EEPROM.get(EEPROM_CONFIG_ADDR, deviceVal);
  
  // 3. Validate the data. If it's a fresh Arduino (reads 0xFFFFFFFF) 
  // or garbage, default back to the DS1742.
  if (deviceVal < (uint32_t)DEV_MAX) {
    g_device = (DeviceType)deviceVal;
  } else {
    g_device = DEV_DS1742; 
  }
}


// ============================================================
// SETUP
// ============================================================

void setup() {
  Serial.begin(115200);
  delay(100);

  // Load the saved device type before touching the pins
  loadDeviceFromEEPROM();

  // Apply the default hardware configuration safely
  applyPinConfiguration();

  Serial.println();
  Serial.println(F("READY"));
}

// ============================================================
// MAIN LOOP
// ============================================================

void loop() {
  showMenu();
  String selection = readLine();
  
  if (selection.length() == 0) return;
  
  switch (selection.charAt(0)) {
    case 'S': handleSelectDevice(); break;
    case 'R': handleReadByte(); break;
    case 'W': handleWriteByte(); break;
    case 'M': handleDumpMemory(); break;
    case 'H': handleDumpHex(); break;
    case 'P': handleProgramHex(); break;
    case 'C': handleClearMemory(); break;
    case 'T': handleRTCStatus(); break;
    case 'I': handleSetTime(); break;
    case 'A': handleSetDate(); break;
    case 'Y': handleSetDay(); break;
    case 'F': handleFrequencyTest(); break;
    default:
      Serial.println(F("INVALID OPTION"));
      break;
  }
}