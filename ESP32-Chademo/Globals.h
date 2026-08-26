#ifndef GLOBALS_H_
#define GLOBALS_H_

//GPIO34 and 35 are input only and have no internal pullup, so an optocoupler board with an open
//collector output leaves them floating and the firmware reads a charge sequence that is not there.
//27 and 13 have pullups, are not strapping pins and are free on this board.
#define CHADEMO_IN1 27
#define CHADEMO_IN2 13

//LC-Relay-ESP32-4R-A2: RY1 drives the charge permission contact, RY2 the contactor coils
#define CHADEMO_OUT1 32
#define CHADEMO_OUT2 33

#define CAN_BAUD 500000
#define minimum(a, b)           (((a) < (b)) ?  (a) : (b))
#define EEPROM_VALID	0xCC

//These have been moved to eeprom. After initial compile the values will be read from EEPROM.
//These thus set the default value to write to eeprom upon first start up
#define MAX_CHARGE_V	160
#define MAX_CHARGE_A	130
#define TARGET_CHARGE_V	158
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

//RY3 and RY4 carry no signal, they exist so the diagnostics page can exercise all four.
#define CHADEMO_OUT3 25
#define CHADEMO_OUT4 26

//Closes the permission contact before the charger puts 12V on the sequence line, for chargers
//that will not start without seeing it. Not stored, so a power cycle always returns to the
//standard order.
extern bool earlyPermission;

extern uint32_t canFrames;
extern uint32_t canLastId;
extern uint32_t canLastMillis;
extern uint32_t canBusOffCount;
extern uint32_t canStatus;

void saveWifi(const char *key, const String &value);

//Defined in the sketch, used by the web server.
void updateTargetAV();
bool chargeInProgress();
bool diagRelayState(int index);
void resetSequence();
void logLine(const char *fmt, ...);
String logDump();
bool diagSetRelay(int index, bool on);
#endif
