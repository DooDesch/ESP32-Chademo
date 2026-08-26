#include "Globals.h"
#include "Chademo.h"
#include "ChademoWebServer.h"
#include "WebSocketPrint.h"

#include <SPIFFS.h>
#include <ACAN_ESP32.h>
#include <EEPROM.h>
#include <WiFi.h>
#include <ESPAsyncWebServer.h>
#include <Preferences.h>
#ifdef DISABLE_BROWNOUT
#include <soc/soc.h>
#include <soc/rtc_cntl_reg.h>
#endif

#define LED_PIN 2
#define HOSTNAME "ESP32-Chademo"
#define AP_SSID "ESP32-CHADEMO"
#define AP_PASSWORD "ChadMeO1"

const unsigned long Interval = 10;
unsigned long Time = 0;
unsigned long ChargeTimeRefSecs = 0; // reference time for charging.
unsigned long PreviousMillis = 0;
unsigned long CurrentMillis = 0;
float Voltage = 0;
float Current = 0;
float Power = 0;
float lastSavedAH = 0;
double ampHourAcc = 0;
double kiloWattHourAcc = 0;
bool earlyPermission = false;
//Ring buffer, because the box runs without a serial cable: the whole point is that a run at a
//charger can be read back afterwards from /log.txt instead of being described from memory.
static char logBuf[6144];
static size_t logPos = 0;
static bool logWrapped = false;
uint32_t canFrames = 0;
uint32_t canBusOffCount = 0;
uint32_t canStatus = 0;
uint32_t canRecoverMillis = 0;
uint32_t canLastId = 0;
uint32_t canLastMillis = 0;
int Count = 0;
int socketMessage = 0;
uint8_t soc;
extern bool overrideStart1 = false;
extern bool overrideStart2 = false;

EESettings settings;
ChademoWebServer chademoWebServer(settings);
WebSocketPrint wsPrint(chademoWebServer.getWebSocket());
CHADEMO chademo(wsPrint);

String cmdStr;
byte Command = 0; // "z" will reset the AmpHours and KiloWattHours counters

uint16_t uint16Val;
uint16_t uint16Val2;
uint8_t uint8Val;
uint8_t print8Val;
uint8_t help8Val;

volatile uint8_t timerIntCounter = 0;
volatile uint8_t timerFastCounter  = 0;
volatile uint8_t timerChademoCounter = 0;

hw_timer_t * timer = NULL;
portMUX_TYPE timerMux = portMUX_INITIALIZER_UNLOCKED;

//
void IRAM_ATTR onTimer() {
  portENTER_CRITICAL_ISR(&timerMux);
  
  timerFastCounter++;
  timerChademoCounter++;
  if (timerChademoCounter >= 4) //25ms tick, so the vehicle frames go out every 100ms
  {
    timerChademoCounter = 0;
    if (chademo.bChademoMode  && chademo.bChademoSendRequests) chademo.bChademoRequest = 1;
  }

  if (timerFastCounter == 8)
  {
    timerFastCounter = 0;
    timerIntCounter++;
    if (timerIntCounter == 18)
    {
      timerIntCounter = 0;
    }
  }
  portEXIT_CRITICAL_ISR(&timerMux);
 
}

void setup() {
  //First thing, before serial and its four second delay: until a pin is driven it sits as a high
  //impedance input and the relay driver sees an undefined level. That left both contactor relays
  //free to pull for seconds after every reset, right through a charger's insulation test.
  digitalWrite(CHADEMO_OUT1, LOW);
  digitalWrite(CHADEMO_OUT2, LOW);
  digitalWrite(CHADEMO_OUT3, LOW);
  digitalWrite(CHADEMO_OUT4, LOW);
  pinMode(CHADEMO_OUT1, OUTPUT);
  pinMode(CHADEMO_OUT2, OUTPUT);
  pinMode(CHADEMO_OUT3, OUTPUT);
  pinMode(CHADEMO_OUT4, OUTPUT);
  digitalWrite(CHADEMO_OUT1, LOW);
  digitalWrite(CHADEMO_OUT2, LOW);

  Serial.begin(115200);
  delay(4000);

  Serial.println("ESP32-CHADEMO");
#ifdef DISABLE_BROWNOUT
  //Bench builds only: an undervolted ESP32 keeps running with undefined outputs, which is the last
  //thing a board driving contactors should do. Never flash this into anything wired to a charger.
  WRITE_PERI_REG(RTC_CNTL_BROWN_OUT_REG, 0);
  Serial.println("BENCH BUILD: brownout detector disabled, do not use on a charger");
#endif

  //Pullup, so an idle optocoupler output reads as no signal instead of floating.
  pinMode(CHADEMO_IN2, INPUT_PULLUP);
  pinMode(CHADEMO_IN1, INPUT_PULLUP);
  //Spare candidates for the inputs, pulled up so the diagnostics page shows a meaningful level.
  pinMode(4, INPUT_PULLUP);
  pinMode(14, INPUT_PULLUP);
  pinMode(LED_PIN, OUTPUT);
  digitalWrite(LED_PIN, HIGH);

  //onboard can
  // Initialize SPIFFS
  if(!SPIFFS.begin(true)){
    Serial.println("An Error has occurred while mounting SPIFFS");
    return;
  }
  
  ACAN_ESP32_Settings canSettings(CAN_BAUD);
  canSettings.mRxPin = GPIO_NUM_16;
  canSettings.mTxPin = GPIO_NUM_17;
  uint16_t errorCode = ACAN_ESP32::can.begin(canSettings);
  if (errorCode > 0) {
    Serial.print ("Can0 Configuration error 0x") ;
    Serial.println (errorCode, HEX) ;
  }
  EEPROM.begin(sizeof(settings));
  EEPROM.get(0, settings);

  if (settings.valid != EEPROM_VALID) //not proper version so reset to defaults
  {
    settings.packSizeKWH = 33.0; //just a random guess. Maybe it should default to zero though?
    settings.valid = EEPROM_VALID;
    settings.ampHours = 0.0;
    settings.kiloWattHours = 0.0; // Is empty kiloWattHours count up
    settings.maxChargeAmperage = MAX_CHARGE_A;
    settings.maxChargeVoltage = MAX_CHARGE_V;
    settings.targetChargeVoltage = TARGET_CHARGE_V;
    settings.minChargeAmperage = MIN_CHARGE_A;
    settings.capacity = CAPACITY;
    settings.debuggingLevel = 1;
    //Both mismatch checks compare the EVSE values against a copy of themselves now that the shunt
    //is gone. They can only produce false aborts, so they start off. Over voltage abort and charge
    //termination do not depend on this flag.
    settings.currentMissmatch = false;
    settings.useBms = false; //a fresh EEPROM reads back as 0xFF, which would select the removed BMS path
    Save();
  }
  //set to false on every boot.
  overrideStart1 = false;
  overrideStart2 = false;

  help8Val = 1;
  print8Val = 1;

  updateTargetAV();

  //Holding the IO0 button at boot restores the built in access point, the way back in after a
  //password nobody remembers.
  pinMode(0, INPUT_PULLUP);
  Preferences prefs;
  prefs.begin("wifi", false);
  if (digitalRead(0) == LOW) {
    prefs.clear();
    Serial.println(F("IO0 held: wifi settings cleared"));
  }
  String apSSID = prefs.getString("apSSID", AP_SSID);
  String apPW = prefs.getString("apPW", AP_PASSWORD);
  String staSSID = prefs.getString("staSSID", "");
  String staPW = prefs.getString("staPW", "");
  earlyPermission = prefs.getBool("early", false);
  prefs.end();

  if (earlyPermission) {
    digitalWrite(CHADEMO_OUT1, HIGH);
    overrideStart1 = true;
    Serial.println(F("Early permission on: contact closed, frames start without d1"));
  }

  WiFi.mode(staSSID.length() ? WIFI_AP_STA : WIFI_AP);
  WiFi.hostname(HOSTNAME);
  WiFi.softAP(apSSID.c_str(), apPW.length() ? apPW.c_str() : NULL);
  if (staSSID.length()) WiFi.begin(staSSID.c_str(), staPW.c_str());
  Serial.print(F("AP "));
  Serial.print(apSSID);
  Serial.print(F(" IP: "));
  Serial.println(WiFi.softAPIP());

  logLine("boot %s %s", __DATE__, __TIME__);
  chademoWebServer.setup();

  /* 1 tick take 1/(80MHZ/80) = 1us so we set divider 80 and count up */
  timer = timerBegin(0, 80, true);
  /* Attach onTimer function to our timer */
  timerAttachInterrupt(timer, &onTimer, true);
 
  /* Set alarm to call onTimer function every second 1 tick is 1us
  => 1 second is 1000000us */
  /* Repeat the alarm (third parameter) */
  timerAlarmWrite(timer, 25000, true);
  timerAlarmEnable(timer);
}

void logLine(const char *fmt, ...)
{
  char line[160];
  unsigned long t = millis();
  int n = snprintf(line, sizeof(line), "%lu.%03lu ", t / 1000, t % 1000);
  va_list args;
  va_start(args, fmt);
  n += vsnprintf(line + n, sizeof(line) - n - 2, fmt, args);
  va_end(args);
  if (n < 0) return;
  if (n > (int)sizeof(line) - 2) n = sizeof(line) - 2;
  line[n++] = 0x0A;
  for (int i = 0; i < n; i++) {
    logBuf[logPos++] = line[i];
    if (logPos >= sizeof(logBuf)) { logPos = 0; logWrapped = true; }
  }
  Serial.write((const uint8_t *)line, n);
}

String logDump()
{
  String out;
  out.reserve(logWrapped ? sizeof(logBuf) : logPos);
  if (logWrapped) for (size_t i = logPos; i < sizeof(logBuf); i++) out += logBuf[i];
  for (size_t i = 0; i < logPos; i++) out += logBuf[i];
  return out;
}

void updateTargetAV()
{
  chademo.setTargetAmperage(settings.maxChargeAmperage);
  chademo.setTargetVoltage(settings.targetChargeVoltage);
}

//True from the moment the plug is detected until the sequence has ended.
bool chargeInProgress()
{
  return chademo.bChademoMode != 0;
}

static const int DIAG_RELAY_PINS[4] = {CHADEMO_OUT1, CHADEMO_OUT2, CHADEMO_OUT3, CHADEMO_OUT4};

bool diagRelayState(int index)
{
  if (index < 0 || index > 3) return false;
  return digitalRead(DIAG_RELAY_PINS[index]) == HIGH;
}

//Arms a stopped or faulted session again without unplugging.
void resetSequence()
{
  chademo.resetSequence();
}

//Manual relay control, refused while a charge sequence owns the outputs.
bool diagSetRelay(int index, bool on)
{
  if (index < 0 || index > 3 || chargeInProgress()) return false;
  digitalWrite(DIAG_RELAY_PINS[index], on ? HIGH : LOW);
  return true;
}


void Save()
{
  Serial.println("Saving EEPROM");
  noInterrupts();
  EEPROM.put(0, settings);
  EEPROM.commit();
  lastSavedAH = settings.ampHours;
  Serial.println (F("SAVED"));
  interrupts();
}

void timestamp()
{
  int milliseconds = (int) (millis() / 1) % 1000 ;
  int seconds = (int) (millis() / 1000) % 60 ;
  int minutes = (int) ((millis() / (1000 * 60)) % 60);
  int hours   = (int) ((millis() / (1000 * 60 * 60)) % 24);

  Serial.print(F(" Time:"));
  Serial.print(hours);
  Serial.print(F(":"));
  Serial.print(minutes);
  Serial.print(F(":"));
  Serial.print(seconds);
  Serial.print(F("."));
  Serial.println(milliseconds);
}

void RngErr() {
  Serial.println(F("Range Error"));
}

void printHelp() {
  help8Val = 0;
  Serial.println(F("Commands"));
  Serial.println(F("h - prints this message"));
  Serial.println(F("z - resets AH/KWH readings"));
  Serial.println(F("p - shows settings"));
  Serial.println(F("u - updates CHAdeMO settings"));
  Serial.println(F("AMIN - Sets min CHAdeMO current"));
  Serial.println(F("AMAX - Sets max CHAdeMO current"));
  Serial.println(F("V - Sets CHAdeMO voltage target"));
  Serial.println(F("VMAX - Sets CHAdeMO max voltage (for protocol reasons)"));
  Serial.println(F("CAPAH - Sets pack amp-hours, shunt provides AmpHours consumed"));
  Serial.println(F("CAPKWH - Sets pack kilowatt-hours"));
  Serial.println(F("DBG - Sets debugging level"));
  Serial.println(F("BMS - Sets use to 0 - No BMS, 1 - ESP32 BMS for SoC and cell voltage and temeratures"));
  Serial.println(F("MISS - Sets use to 0 - Disable Current Miss-Match check, 1 - Enable Current Miss-Match check"));
  Serial.println(F("OVER1 - Sets use to 0 - Disable Override Start 1, 1 - Enable Override Start 1"));
  Serial.println(F("OVER2 - Sets use to 0 - Disable Override Start 2, 1 - Enable Override Start 2"));


  Serial.println(F("Example: V=395 - sets CHAdeMO voltage target to 395"));
}

void ParseCommand() {
  float fltVal = 0;
  if (cmdStr == "V") {
    uint16Val = Serial.parseInt();
    if (uint16Val > 0 && uint16Val < 1000) {
      settings.targetChargeVoltage = uint16Val;
      Save();
      print8Val = 1;
    } else {
      RngErr();
    }
  }
  else if (cmdStr == "VMAX") {
    uint16Val = Serial.parseInt();
    if (uint16Val > 0 && uint16Val < 1000) {
      settings.maxChargeVoltage = uint16Val;
      Save();
      print8Val = 1;
    } else {
      RngErr();
    }
  }
  else if (cmdStr == "AMIN") {
    uint8Val = Serial.parseInt();
    if (uint8Val > 0 && uint8Val < 256) {
      settings.minChargeAmperage = uint8Val;
      Save();
      print8Val = 1;
    } else {
      RngErr();
    }
  }
  else if (cmdStr == "AMAX") {
    uint8Val = Serial.parseInt();
    if (uint8Val > 0 && uint8Val < 256) {
      settings.maxChargeAmperage = uint8Val;
      Save();
      print8Val = 1;
    } else {
      RngErr();
    }
  }
  else if (cmdStr == "CAPAH") {
    uint8Val = Serial.parseInt();
    if (uint8Val > 0 && uint8Val < 256) {
      settings.capacity = uint8Val;
      Save();
      print8Val = 1;
    } else {
      RngErr();
    }
  }
  else if (cmdStr == "BMS") {
    uint8Val = Serial.parseInt();
    if (uint8Val >= 0 && uint8Val < 2) {
      settings.useBms = uint8Val == 1;
      Save();
      print8Val = 1;
    } else {
      RngErr();
    }
  }
  else if (cmdStr == "MISS") {
    uint8Val = Serial.parseInt();
    if (uint8Val >= 0 && uint8Val < 2) {
      settings.currentMissmatch = uint8Val == 1;
      Save();
      print8Val = 1;
    } else {
      RngErr();
    }
  } 
  else if (cmdStr == "OVER1") {
    uint8Val = Serial.parseInt();
    if (uint8Val >= 0 && uint8Val < 2) {
      overrideStart1 = uint8Val == 1;
      Save();
      print8Val = 1;
    } else {
      RngErr();
    }
  }
  else if (cmdStr == "OVER2") {
    uint8Val = Serial.parseInt();
    if (uint8Val >= 0 && uint8Val < 2) {
      overrideStart2 = uint8Val == 1;
      Save();
      print8Val = 1;
    } else {
      RngErr();
    }
  }
  else if (cmdStr == "CAPKWH") {
    fltVal = Serial.parseFloat();
    if (fltVal > 0 && fltVal < 100) {
      settings.packSizeKWH = fltVal;
      Save();
      print8Val = 1;
    } else {
      RngErr();
    }
  }
  else if (cmdStr == "DBG") {
    uint8Val = Serial.parseInt();
    if (uint8Val >= 0 && uint8Val < 4) {
      settings.debuggingLevel = uint8Val;
      Save();
      print8Val = 1;
    } else {
      RngErr();
    }
  }
  else if (cmdStr == "SAVE") {
    Save();
  }
  else if (cmdStr == "FULLRESET") {
    Serial.println(F("Resetting EEPROM"));
    settings.valid = 0;
    Save();
  }
  else {
    help8Val = 1;
  }
}


void SerialCommand()
{
  int available = Serial.available();
  if (available > 0)
  {
    Command = Serial.peek();
    
    if (Command == 'p')
    {
      print8Val = 1;
    }
    else if (Command == 'u')
    {
      updateTargetAV();
    }
    else if (Command == 'h') {
      help8Val = 1;
    }
    else {
      cmdStr = Serial.readStringUntil('=');
      ParseCommand();
    }
    while (Serial.available() > 0) Serial.read();
  }
}

void printSettings() {
  print8Val = 0;
  Serial.println (F("-"));
  Serial.print (F("AH "));
  Serial.println (settings.ampHours);
  Serial.print (F("KWH "));
  Serial.println (settings.kiloWattHours);
  Serial.print (F("AMIN "));
  Serial.println (settings.minChargeAmperage, 1);
  Serial.print (F("AMAX "));
  Serial.println (settings.maxChargeAmperage, 1);
  Serial.print (F("V "));
  Serial.println (settings.targetChargeVoltage, 1);
  Serial.print (F("VMAX "));
  Serial.println (settings.maxChargeVoltage, 1);
  Serial.print (F("CAPAH "));
  Serial.println (settings.capacity, 1);
  Serial.print (F("CAPKWH "));
  Serial.println (settings.packSizeKWH, 2);
  Serial.print (F("BMS "));
  Serial.println (settings.useBms, 1);
  Serial.print (F("MISS "));
  Serial.println (settings.currentMissmatch, 1);
  Serial.print (F("DBG "));
  Serial.println (settings.debuggingLevel, 1);
  Serial.println (F("-"));
}

void outputState() {

  Serial.print (F("ESP32: "));
  Serial.print (Voltage, 3);
  Serial.print (F("v "));
  Serial.print (Current, 2);
  Serial.print (F("A "));
  Serial.print (settings.ampHours, 1);
  Serial.print (F("Ah "));
  Serial.print (Power, 1);
  Serial.print (F("kW "));
  Serial.print (settings.kiloWattHours, 1);
  Serial.print (F("kWh "));
  Serial.print (F("OUT1"));
  Serial.print (digitalRead(CHADEMO_OUT1) > 0 ? F(":1 ") : F(":0 "));
  Serial.print (F("OUT2"));
  Serial.print (digitalRead(CHADEMO_OUT2) > 0 ? F(":1 ") : F(":0 "));
  Serial.print (F("IN1"));
  Serial.print (!digitalRead(CHADEMO_IN1) > 0 ? F(":1 ") : F(":0 "));
  Serial.print (F("IN2"));
  Serial.print (!digitalRead(CHADEMO_IN2) > 0 ? F(":1 ") : F(":0 "));
  Serial.print (F("OVER1"));
  Serial.print (overrideStart1 > 0 ? F(":1 ") : F(":0 "));
  Serial.print (F("OVER2"));
  Serial.print (overrideStart2 > 0 ? F(":1 ") : F(":0 "));
  Serial.print (F("CHG T: "));
  Serial.println (CurrentMillis / 1000 - ChargeTimeRefSecs);
}

void broadcastMessage() {
  DynamicJsonDocument json(1024);
  char buffer[1024]; // create temp buffer
  switch(socketMessage) {
    case 0: {
      json["voltage"] = Voltage;
      json["amperage"] = Current;
      json["power"] = Power;
      json["ampHours"] = settings.ampHours;
      json["soc"] = soc;

      size_t len = serializeJson(json, buffer);  // serialize to buffe
      chademoWebServer.getWebSocket().textAll(buffer, len);
    }
    case 1: {
      //chademo status
      json["chademoState"] = chademo.getState();
      json["EVSEAvailableVoltage"] = chademo.getEVSEParams().availVoltage;
      json["EVSEAvailableCurrent"] = chademo.getEVSEParams().availCurrent;
      json["EVSEThresholdVoltage"] = chademo.getEVSEParams().thresholdVoltage;

      size_t len = serializeJson(json, buffer);  // serialize to buffe
      chademoWebServer.getWebSocket().textAll(buffer, len);
    }
    case 2: {
      //car status
      json["OVER1"] = overrideStart1;
      json["OVER2"] = overrideStart2;
      json["OUT1"] = digitalRead(CHADEMO_OUT1);
      json["OUT2"] = digitalRead(CHADEMO_OUT2);
      json["IN1"] = !digitalRead(CHADEMO_IN1);
      json["IN2"] =!digitalRead(CHADEMO_IN2);

      size_t len = serializeJson(json, buffer);  // serialize to buffe
      chademoWebServer.getWebSocket().textAll(buffer, len);
    }
  }
  socketMessage++;
  if (socketMessage > 2) {
    socketMessage = 0;
  }
}

void loop() {

  uint8_t pos;
  CurrentMillis = millis();
  uint8_t len;
  CANMessage inFrame;
  float tempReading;

#ifdef DEBUG_TIMING
  if (debugTick == 1)
  {
    debugTick = 0;
    Serial.println(millis());
  }
#endif
  chademo.loop();
  chademoWebServer.execute();
  if (CurrentMillis - PreviousMillis >= Interval)
  {
    Time = CurrentMillis - PreviousMillis;
    PreviousMillis = CurrentMillis;

    Count++;
    //There is no shunt in this build, so the EVSE reported values are the only source.
    //Sign follows the shunt convention the state machine expects: negative while charging.
    if (chademo.getState() == RUNNING)
    {
      Voltage = chademo.getEVSEStatus().presentVoltage;
      Current = chademo.getEVSEStatus().presentCurrent * -1.0;
    }
    else
    {
      //Outside a running session the EVSE values are stale, so nothing is counted.
      Voltage = 0;
      Current = 0;
    }
    Power = Voltage * Current / 1000.0;

    //Charge termination, the serial reset and the settings page all write the counters directly,
    //so pick those values up before adding to them.
    if (settings.ampHours != (float)ampHourAcc) ampHourAcc = settings.ampHours;
    if (settings.kiloWattHours != (float)kiloWattHourAcc) kiloWattHourAcc = settings.kiloWattHours;

    //Count down kiloWattHours when drawing current.
    //Count up when charging (Current/Power is negative)
    //Accumulated as double: a float counter at 180Ah cannot resolve a 10ms increment.
    ampHourAcc += (double)Current * Time / 3600000.0;
    kiloWattHourAcc -= (double)Power * Time / 3600000.0;
    settings.ampHours = ampHourAcc;
    settings.kiloWattHours = kiloWattHourAcc;

    chademo.doProcessing();

    if (Count >= 50)
    {
      digitalWrite(LED_PIN, !digitalRead(LED_PIN));
      Count = 0;
      SerialCommand();
      broadcastMessage();

      if (print8Val > 0)
        printSettings();
      else
        outputState();
      if (help8Val > 0)
        printHelp();
      if (chademo.bChademoMode)
      {
        if (settings.debuggingLevel > 0) {
          Serial.print(F("Chademo Mode: "));
          Serial.println(chademo.getState());
        }
      }
    }
  }
  //Everything worth reconstructing later is polled here rather than sprinkled through the state
  //machine: state changes, both sequence signals, both relays, and what the charger sends.
  {
    static const char *STATE_NAMES[] = {"STARTUP", "SEND_PARAMS", "WAIT_PARAMS", "SET_BEGIN",
      "WAIT_CONFIRM", "CLOSE_CONTACTORS", "RUNNING", "CEASE_CURRENT", "WAIT_ZERO_CURRENT",
      "OPEN_CONTACTOR", "FAULTED", "STOPPED", "LIMBO"};
    static int lastState = -1;
    static int lastIn1 = -1, lastIn2 = -1, lastOut1 = -1, lastOut2 = -1;

    int st = chademo.getState();
    if (st != lastState) {
      lastState = st;
      logLine("state %s", st >= 0 && st <= 12 ? STATE_NAMES[st] : "?");
    }
    int in1 = !digitalRead(CHADEMO_IN1), in2 = !digitalRead(CHADEMO_IN2);
    if (in1 != lastIn1) { lastIn1 = in1; logLine("d1 %s", in1 ? "aktiv" : "ruhig"); }
    if (in2 != lastIn2) { lastIn2 = in2; logLine("d2 %s", in2 ? "aktiv" : "ruhig"); }
    int out1 = digitalRead(CHADEMO_OUT1), out2 = digitalRead(CHADEMO_OUT2);
    if (out1 != lastOut1) { lastOut1 = out1; logLine("RY1 %s", out1 ? "zu" : "auf"); }
    if (out2 != lastOut2) { lastOut2 = out2; logLine("RY2 %s", out2 ? "zu" : "auf"); }
  }

  //A CAN node alone on the bus never gets an acknowledge, so its error counters climb until the
  //controller switches itself off. It is then deaf as well as mute, which looks exactly like a
  //broken transceiver. Recovering brings it back the moment the charger starts talking.
  canStatus = ACAN_ESP32::can.statusFlags();
  if ((canStatus & 0x04) && (millis() - canRecoverMillis > 50)) {
    canRecoverMillis = millis();
    //Only count what actually recovered: recovery needs an idle bus and fails until it sees one.
    if (ACAN_ESP32::can.recoverFromBusOff()) canBusOffCount++;
  }

  if (ACAN_ESP32::can.receive(inFrame)) {
    canFrames++;
    canLastId = inFrame.id;
    canLastMillis = millis();

    //0x108 arrives rarely and matters every time; 0x109 repeats at 100ms, so only a change in
    //what the charger reports is worth a line.
    if (inFrame.id == 0x108) {
      logLine("0x108 verfuegbar %d V %d A schwelle %d V",
              inFrame.data[1] + inFrame.data[2] * 256, inFrame.data[3],
              inFrame.data[4] + inFrame.data[5] * 256);
    } else if (inFrame.id == 0x109) {
      static int lastV = -1, lastA = -1, lastStatus = -1;
      int v = inFrame.data[1] + inFrame.data[2] * 256;
      int a = inFrame.data[3];
      int status = inFrame.data[5];
      if (v != lastV || a != lastA || status != lastStatus) {
        lastV = v; lastA = a; lastStatus = status;
        logLine("0x109 %d V %d A status 0x%02X", v, a, status);
      }
    } else {
      static uint32_t lastOther = 0;
      if (millis() - lastOther > 1000) { lastOther = millis(); logLine("frame 0x%03X", inFrame.id); }
    }

    chademo.handleCANFrame(inFrame);
  }
}
