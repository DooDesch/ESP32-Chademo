#ifndef GLOBALS_H_
#define GLOBALS_H_

#define CHADEMO_IN1 34
#define CHADEMO_IN2 35

//LC-Relay-ESP32-4R-A2: RY1 drives the charge permission contact, RY2 the contactor coils
#define CHADEMO_OUT1 32
#define CHADEMO_OUT2 33

#define CAN_BAUD 500000
#define minimum(a, b)           (((a) < (b)) ?  (a) : (b))
#define EEPROM_VALID	0xCC

//These have been moved to eeprom. After initial compile the values will be read from EEPROM.
//These thus set the default value to write to eeprom upon first start up
#define MAX_CHARGE_V	158
#define MAX_CHARGE_A	130
#define TARGET_CHARGE_V	160
#define MIN_CHARGE_A	20
#define CAPACITY 180

typedef struct
{
  uint8_t valid; //a token to store EEPROM version and validity. If it matches expected value then EEPROM is not reset to defaults //0
  float ampHours; //floats are 4 bytes //1
  float kiloWattHours; //5
  float packSizeKWH; //9
  uint16_t maxChargeVoltage; //21
  uint16_t targetChargeVoltage; //23
  uint8_t maxChargeAmperage; //25
  uint8_t minChargeAmperage; //26
  uint8_t capacity; //27
  uint8_t debuggingLevel; //29
  bool useBms;
  bool currentMissmatch;

} EESettings;

extern EESettings settings;
extern float Voltage;
extern float Current;
extern unsigned long CurrentMillis;
extern int Count;
extern bool overrideStart1;
extern bool overrideStart2;
#endif
