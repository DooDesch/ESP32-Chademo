/*
  chademo.cpp - Houses all chademo related functionality
*/
#include <ACAN_ESP32.h>
#include "chademo.h"
#include <Arduino.h>

template<class T> inline Print &operator <<(Print &obj, T arg) {
  obj.print(arg);  //Sets up serial streaming Serial<<someshit;
  return obj;
}
void timestamp();

//Writes a vehicle frame to the log whenever its content changes, so what actually goes on the bus
//can be read back instead of being reconstructed from the source.
static void logFrameOnChange(const CANMessage &frame)
{
  static uint8_t last[3][8];
  static bool seen[3] = {false, false, false};
  int slot = frame.id == 0x100 ? 0 : frame.id == 0x101 ? 1 : frame.id == 0x102 ? 2 : -1;
  if (slot < 0) return;
  if (seen[slot] && memcmp(last[slot], frame.data, 8) == 0) return;
  memcpy(last[slot], frame.data, 8);
  seen[slot] = true;
  logLine("sende 0x%03X %02X %02X %02X %02X %02X %02X %02X %02X", frame.id,
          frame.data[0], frame.data[1], frame.data[2], frame.data[3],
          frame.data[4], frame.data[5], frame.data[6], frame.data[7]);
}

CHADEMO::CHADEMO(WebSocketPrint& webSocketPrint) : webSocketPrint { webSocketPrint }
{
  bStartedCharge = 0;
  bChademoMode = 0;
  bChademoSendRequests = 0;
  bChademoRequest = 0;
  //CHAdeMO wants the protocol number fixed from the first frame to the end of the session,
  //so it is announced as 1.0 from the start instead of switching once the charger answers.
  bChademo10Protocol = 1;
  bConnectorLocked = 0;
  askingAmps = 0;
  bListenEVSEStatus = 0;
  bDoMismatchChecks = 0;
  insertionTime = 0;
  chademoState = STOPPED;
  stateHolder = STOPPED;
  carStatus.contactorOpen = 1;
  carStatus.battOverTemp = 0;
  carStatus.derated = 1;

}

//will wait delayTime milliseconds and then transition to new state. Sets state to LIMBO in the meantime
void CHADEMO::setDelayedState(int newstate, uint16_t delayTime)
{
  chademoState = LIMBO;
  stateHolder = (CHADEMOSTATE)newstate;
  stateMilli = millis();
  stateDelay = delayTime;
}

CHADEMOSTATE CHADEMO::getState()
{
  return chademoState;
}

EVSE_STATUS CHADEMO::getEVSEStatus() {
  return evse_status;
}

EVSE_PARAMS CHADEMO::getEVSEParams() {
  return evse_params;
}

void CHADEMO::setTargetAmperage(uint8_t t_amp)
{
  carStatus.targetCurrent = t_amp;
}

void CHADEMO::setTargetVoltage(uint16_t t_volt)
{
  carStatus.targetVoltage = t_volt;
}

//Lets a stopped or faulted session be armed again without unplugging: the sequence only rearms
//when the charge sequence line drops, which at a charger that keeps it energised never happens.
void CHADEMO::resetSequence()
{
  bStartedCharge = 0;
  bChademoSendRequests = 0;
  bListenEVSEStatus = 0;
  bDoMismatchChecks = 0;
  faultCount = 0;
  vOverFault = 0;
  carStatus.battOverVolt = 0;
  carStatus.battUnderVolt = 0;
  carStatus.chargingFault = 0;
  carStatus.currDeviation = 0;
  carStatus.voltDeviation = 0;
  carStatus.stopRequest = 0;
  chademoState = STOPPED;
  digitalWrite(CHADEMO_OUT2, LOW);
  Serial.println(F("Sequence reset"));
}

void CHADEMO::setChargingFault()
{
  carStatus.chargingFault = 1;
}

void CHADEMO::setBattOverTemp()
{
  carStatus.battOverTemp = 1;
}

//stuff that should be frequently run (as fast as possible)
void CHADEMO::loop()
{
  if (!digitalRead(CHADEMO_IN1) || overrideStart1) //IN1 goes LOW if we have been plugged into the chademo port
  {
    if (insertionTime == 0)
    {
      //TM - Set the outputs LOW in case they have been set
      //     outside of Chademo Mode.
      digitalWrite(CHADEMO_OUT2, LOW);
      //Some chargers wait for the permission contact before they put 12V on the sequence line,
      //so early permission has to survive this reset or the two sides wait for each other.
      if (!earlyPermission) digitalWrite(CHADEMO_OUT1, LOW);

      insertionTime = millis();
    }
    else if (millis() > (uint32_t)(insertionTime + 30)) //CHAdeMO allows 500ms from d1 to the first frame
    {
      if (bChademoMode == 0)
      {
        bChademoMode = 1;
        if (chademoState == STOPPED && !bStartedCharge) {
          chademoState = STARTUP;
          Serial.println(F("Starting Chademo process."));
          webSocketPrint.message(F("Starting Chademo process."));
          carStatus.battOverTemp = 0;
          carStatus.battOverVolt = 0;
          carStatus.battUnderVolt = 0;
          carStatus.chargingFault = 0;
          carStatus.chargingEnabled = 0;
          carStatus.contactorOpen = 1;
          carStatus.currDeviation = 0;
          carStatus.notParked = 0;
          carStatus.stopRequest = 0;
          carStatus.voltDeviation = 0;
          bChademo10Protocol = force09 ? 0 : 1;
        }
      }
    }
  }
  else
  {
    insertionTime = 0;
    if (bChademoMode == 1)
    {
      Serial.println(F("Stopping chademo process."));
      webSocketPrint.message(F("Stopping chademo process."));
      bChademoMode = 0;
      bStartedCharge = 0;
      chademoState = STOPPED;
      //maybe it would be a good idea to try to see if EVSE is still transmitting to us and providing current
      //as it is not a good idea to open the contactors under load. But, IN1 shouldn't trigger
      //until the EVSE is ready. Also, the EVSE should have us locked so the only way the plug should come out under
      //load is if the idiot driver took off in the car. Bad move moron.
      digitalWrite(CHADEMO_OUT2, LOW);
      if (!earlyPermission) digitalWrite(CHADEMO_OUT1, LOW);
      if (settings.debuggingLevel > 0)
      {
        Serial.println(F("CAR: Contactor open"));
        Serial.println(F("CAR: Charge Enable OFF"));
        webSocketPrint.message(F("CAR: Contactor open"));
        webSocketPrint.message(F("CAR: Charge Enable OFF"));
  
      }
    }
  }

  if (bChademoMode)
  {

    if (!bDoMismatchChecks && chademoState == RUNNING)
    {
      if (settings.currentMissmatch && (CurrentMillis - mismatchStart) >= mismatchDelay) {
        bDoMismatchChecks = 1;
      }
    }

    if (chademoState == LIMBO && (CurrentMillis - stateMilli) >= stateDelay)
    {
      chademoState = stateHolder;
    }

    if (bChademoSendRequests && bChademoRequest)
    {
      bChademoRequest = 0;
      //CHAdeMO expects every vehicle frame every 100ms. Rotating one frame per request stretched
      //each ID out to 225ms, which an EVSE can treat as a communication fault.
      sendCANStatus();
      sendCANBattSpecs();
      sendCANChargingTime();
    }

    switch (chademoState)
    {
      case STARTUP:
        bDoMismatchChecks = 0; //reset it for now
        insulationSeen = 0;
        chademoState = SEND_INITIAL_PARAMS; //no delay, the charger expects frames within 500ms of d1
        break;
      case SEND_INITIAL_PARAMS:
        //we could do calculations to see how long the charge should take based on SOC and
        //also set a more realistic starting amperage. Options for the future.
        //One problem with that is that we don't yet know the EVSE parameters so we can't know
        //the max allowable amperage just yet.
        bChademoSendRequests = 1; //causes chademo frames to be sent out every 100ms
        chademoState = WAIT_FOR_EVSE_PARAMS;
        if (settings.debuggingLevel > 0) {
          Serial.println(F("Sent params to EVSE. Waiting."));
          webSocketPrint.message(F("Sent params to EVSE. Waiting."));

        }
        break;
      case WAIT_FOR_EVSE_PARAMS:
        //for now do nothing while we wait. Might want to try to resend start up messages periodically if no reply
        break;
      case SET_CHARGE_BEGIN:
        if (settings.debuggingLevel > 0) {
          Serial.println(F("CAR:Charge enable ON"));
          webSocketPrint.message(F("CAR:Charge enable ON"));

        }
        digitalWrite(CHADEMO_OUT1, HIGH); //signal that we're ready to charge
        //The CAN bit follows the contact rather than announcing permission the charger cannot yet
        //see: it is set once the relay has had its 50ms to actually close.
        setDelayedState(WAIT_FOR_BEGIN_CONFIRMATION, 50);
        break;
      case WAIT_FOR_BEGIN_CONFIRMATION:
        carStatus.chargingEnabled = 1;
        //skipD2 is for a charger that runs the whole sequence but never asserts the second sequence
        //signal. Guarded on the charger's own reported output: closing while it still stands at its
        //insulation test voltage would put hundreds of volts of difference across the contactors.
        //Below 20V is also true before the charger has done anything at all, and closing there put
        //the pack on its terminals while it was still preparing, which it answers with a fault.
        //So wait until its insulation test has actually run and its output has come back down.
        if (evse_status.presentVoltage > 100) insulationSeen = 1;
        if (!digitalRead(CHADEMO_IN2) || overrideStart2 ||
            (skipD2 && insulationSeen && evse_status.presentVoltage < 20
             && (evse_status.status & EVSE_STATUS_CONNLOCK)))
        {
          setDelayedState(CLOSE_CONTACTORS, 100);
        }
        break;
      case CLOSE_CONTACTORS:
        if (settings.debuggingLevel > 0) {
          Serial.println(F("CAR:Contactor close."));
          webSocketPrint.message(F("CAR:Contactor close."));
        }
        digitalWrite(CHADEMO_OUT2, HIGH);
        setDelayedState(RUNNING, 50);
        carStatus.contactorOpen = 0; //its closed now
        carStatus.chargingEnabled = 1; //please sir, I'd like some charge
        bStartedCharge = 1;
        mismatchStart = millis();
        break;
      case RUNNING:
        //do processing here by taking our measured voltage, amperage, and SOC to see if we should be commanding something
        //different to the EVSE. Also monitor temperatures to make sure we're not incinerating the pack.
        break;
      case CEASE_CURRENT:
        if (settings.debuggingLevel > 0){
          Serial.println(F("CAR:Current req to 0"));
          webSocketPrint.message(F("CAR:Current req to 0"));
        }
        carStatus.targetCurrent = 0;
        stateMilli = millis();
        chademoState = WAIT_FOR_ZERO_CURRENT;
        break;
      case WAIT_FOR_ZERO_CURRENT:
        //A charger that has already ended the session stops sending 0x109, and the last reported
        //current then stands forever. Without this the box waits here for good: contactors closed,
        //no way back to STOPPED, and the next plug in does nothing.
        if (evse_status.presentCurrent == 0 || (millis() - stateMilli) > 3000)
        {
          setDelayedState(OPEN_CONTACTOR, 150);
        }
        break;
      case OPEN_CONTACTOR:
        if (settings.debuggingLevel > 0) {
          Serial.println(F("CAR:OPEN Contacor"));
          webSocketPrint.message(F("CAR:OPEN Contacor"));
        }
        digitalWrite(CHADEMO_OUT2, LOW);
        digitalWrite(CHADEMO_OUT2, LOW);
        carStatus.contactorOpen = 1;
        carStatus.chargingEnabled = 0;
        sendCANStatus(); //we probably need to force this right now
        setDelayedState(STOPPED, 100);
        break;
      case FAULTED:
        Serial.println(F("CAR: fault!"));
        webSocketPrint.message(F("CAR: fault!"));
        chademoState = CEASE_CURRENT;
        //digitalWrite(OUT2, LOW);
        //digitalWrite(OUT1, LOW);
        break;
      case STOPPED:
        if (bChademoSendRequests == 1)
        {
          digitalWrite(CHADEMO_OUT1, LOW);
          digitalWrite(CHADEMO_OUT2, LOW);
          if (settings.debuggingLevel > 0)
          {
            Serial.println(F("CAR:Contactor OPEN"));
            Serial.println(F("CAR:Charge Enable OFF"));
            webSocketPrint.message(F("CAR:Contactor OPEN"));
            webSocketPrint.message(F("CAR:Charge Enable OFF"));

          }
          bChademoSendRequests = 0; //don't need to keep sending anymore.
          bListenEVSEStatus = 0; //don't want to pay attention to EVSE status when we're stopped
        }
        break;
    }
  }
}

//things that are less frequently run - run on a set schedule
void CHADEMO::doProcessing()
{
  uint8_t tempCurrVal;

  if (chademoState == RUNNING && ((CurrentMillis - lastCommTime) >= lastCommTimeout))
  {
    //this is BAD news. We can't do the normal cease current procedure because the EVSE seems to be unresponsive.
    Serial.println(F("EVSE comm fault! Commencing emergency shutdown!"));
    webSocketPrint.message(F("EVSE comm fault! Commencing emergency shutdown!"));
    //yes, this isn't ideal - this will open the contactor and send the shutdown signal. It's better than letting the EVSE
    //potentially run out of control.
    chademoState = OPEN_CONTACTOR;
  }

  //Over voltage abort and the charge termination logic must not depend on the mismatch gate:
  //that gate is delayed by mismatchDelay and can be switched off entirely from the settings page.
  if (chademoState == RUNNING)
  {
    if (Voltage > settings.maxChargeVoltage && !carStatus.battOverVolt)
    {
      vOverFault++;
      if (vOverFault > 9)
      {
        Serial.println(F("Over voltage fault!"));
        webSocketPrint.message(F("Over voltage fault!"));
        carStatus.battOverVolt = 1;
        chademoState = CEASE_CURRENT;
      }
    }
    else vOverFault = 0;

    //Constant Current/Constant Voltage Taper checks.  If minimum current is set to zero, we terminate once target voltage is reached.
    //If not zero, we will adjust current up or down as needed to maintain voltage until current decreases to the minimum entered

    if (Count == 20)
    {
      if (evse_status.presentVoltage > settings.targetChargeVoltage - 1) //All initializations complete and we're running.We've reached charging target
      {
       
        if (settings.minChargeAmperage == 0 || carStatus.targetCurrent < settings.minChargeAmperage) {
          //putt SOC, ampHours and kiloWattHours reset in here once we actually reach the termination point.
          settings.ampHours = 0; // Amp hours count up as used
          settings.kiloWattHours = 0; // Kilowatt Hours count up as used.
          chademoState = CEASE_CURRENT;  //Terminate charging
        } else
          carStatus.targetCurrent--;  //Taper. Actual decrease occurs in sendChademoStatus
      }
      else //Only adjust upward if we have previous adjusted downward and do not exceed max amps
      {
        if (carStatus.targetCurrent < settings.maxChargeAmperage) carStatus.targetCurrent++;
      }
    }
  }

}

void CHADEMO::handleCANFrame(CANMessage &frame)
{
  uint8_t tempCurrVal;
  uint16_t tempAvailCurr;

  if (frame.id == EVSE_PARAMS_ID)
  {
    lastCommTime = millis();
    
    if (chademoState == WAIT_FOR_EVSE_PARAMS) setDelayedState(SET_CHARGE_BEGIN, 100);
    evse_params.supportWeldCheck = frame.data[0];
    evse_params.availVoltage = frame.data[1] + frame.data[2] * 256;
    evse_params.availCurrent = frame.data[3];
    evse_params.thresholdVoltage = frame.data[4] + frame.data[5] * 256;

    // Workaround for dunedin charger. If we ask for exactly what it
    // Says is available then it packs a sad.
    // So we'll stay on amp less then it says it has. - TAM
    tempAvailCurr = evse_params.availCurrent > 0 ? evse_params.availCurrent - 1 : 0;


    if (settings.debuggingLevel > 1)
    {
      
      Serial.print(F("EVSE: MaxVoltage: "));
      Serial.print(evse_params.availVoltage);
      Serial.print(F(" Max Current:"));
      Serial.print(evse_params.availCurrent);
      Serial.print(F(" Threshold Voltage:"));
      Serial.print(evse_params.thresholdVoltage);
      timestamp();
    }

    //if charger cannot provide our requested voltage then GTFO
    if (evse_params.availVoltage < carStatus.targetVoltage && chademoState <= RUNNING)
    {
      vCapCount++;
      if (vCapCount > 9)
      {
        Serial.print(F("EVSE can't provide needed voltage. Aborting."));
        Serial.println(evse_params.availVoltage);
        webSocketPrint.message("EVSE can't provide needed voltage (" + String(evse_params.availVoltage) + "). Aborting.");
        chademoState = CEASE_CURRENT;
      }
    }
    else vCapCount = 0;

    //if we want more current then it can provide then revise our request to match max output
    if (tempAvailCurr < carStatus.targetCurrent) carStatus.targetCurrent = tempAvailCurr;

    //If not in running then also change our target current up to the minimum between the
    //available current reported and the max charge amperage. This should fix an issue where
    //the target current got wacked for some reason and left at zero.
    if (chademoState != RUNNING && tempAvailCurr > carStatus.targetCurrent)
    {
      carStatus.targetCurrent = minimum(tempAvailCurr, settings.maxChargeAmperage);
    }
  }

  if (frame.id == EVSE_STATUS_ID)
  {
   
    lastCommTime = millis();
    //Deliberately not adopting the charger's protocol number: changing it mid session is what
    //the specification forbids, and the vehicle side stays on the value it started with.
    evse_status.presentVoltage = frame.data[1] + 256 * frame.data[2];
    evse_status.presentCurrent  = frame.data[3];
    evse_status.status = frame.data[5];
    bConnectorLocked = (frame.data[5] >> 2) & 0x01;

    
    if (frame.data[6] < 0xFF)
    {
      evse_status.remainingChargeSeconds = frame.data[6] * 10;
    }
    else
    {
      evse_status.remainingChargeSeconds = frame.data[7] * 60;
    }

    if (chademoState == RUNNING && bDoMismatchChecks)
    {
      if (abs(Voltage - evse_status.presentVoltage) > (evse_status.presentVoltage >> 3) && !carStatus.voltDeviation)
      {
        vMismatchCount++;
        if (vMismatchCount > 4)
        {
          Serial.print(F("Voltage mismatch! Aborting! Reported: "));
          Serial.print(evse_status.presentVoltage);
          Serial.print(F(" Measured: "));
          Serial.println(Voltage);
          webSocketPrint.message("Voltage mismatch! Aborting! Reported:" + String(evse_status.presentVoltage) + "  Measured: " + String(Voltage));

          carStatus.voltDeviation = 1;
          chademoState = CEASE_CURRENT;
        }
      }
      else vMismatchCount = 0;

      tempCurrVal = evse_status.presentCurrent >> 3;
      if (tempCurrVal < 3) tempCurrVal = 3;
      if (abs((Current * -1.0) - evse_status.presentCurrent) > tempCurrVal && !carStatus.currDeviation)
      {
        cMismatchCount++;
        if (cMismatchCount > 4)
        {
          Serial.print(F("Current mismatch! Aborting! Reported: "));
          Serial.print(evse_status.presentCurrent);
          Serial.print(F(" Measured: "));
          Serial.println(Current * -1.0);
          
          webSocketPrint.message("Current mismatch! Aborting! Reported:" + String(evse_status.presentCurrent) + "  Measured: " + String((Current * -1.0)));

          carStatus.currDeviation = 1;
          chademoState = CEASE_CURRENT;
        }
      }
      else cMismatchCount = 0;
    }

    if (settings.debuggingLevel > 1)
    {
      Serial.print(F("EVSE: Measured Voltage: "));
      Serial.print(evse_status.presentVoltage);
      Serial.print(F(" Current: "));
      Serial.print(evse_status.presentCurrent);
      Serial.print(F(" Time remaining: "));
      Serial.print(evse_status.remainingChargeSeconds);
      Serial.print(F(" Status: "));
      Serial.print(evse_status.status, BIN);
      timestamp();
    }


    //on fault try to turn off current immediately and cease operation
    if ((evse_status.status & 0x1A) != 0) //if bits 1, 3, or 4 are set then we have a problem.
    {
      faultCount++;
      if (faultCount > 3)
      {
        Serial.print(F("EVSE:fault code "));
        Serial.print(evse_status.status,HEX);
        Serial.println(F(" Abort."));
        webSocketPrint.message("EVSE:fault code " + evse_status.status);

        if (chademoState == RUNNING) chademoState = CEASE_CURRENT;
      }
    }
    else faultCount = 0;

    if (chademoState == RUNNING)
    {
      if (bListenEVSEStatus)
      {
        if ((evse_status.status & EVSE_STATUS_STOPPED) != 0)
        {
          Serial.println(F("EVSE:stop charging."));
          webSocketPrint.message(F("EVSE:stop charging."));

          chademoState = CEASE_CURRENT;
        }

        //if there is no remaining time then gracefully shut down
        if (evse_status.remainingChargeSeconds == 0)
        {
          Serial.println(F("EVSE:time elapsed..Ending"));
          webSocketPrint.message(F("EVSE:time elapsed..Ending"));
          chademoState = CEASE_CURRENT;
        }
      }
      else
      {
        //if charger is not reporting being stopped and is reporting remaining time then enable the checks.
        if ((evse_status.status & EVSE_STATUS_STOPPED) == 0 && evse_status.remainingChargeSeconds > 0) bListenEVSEStatus = 1;
      }
    }
  }

}

void CHADEMO::sendCANBattSpecs()
{
  CANMessage outFrame;
  outFrame.id = CARSIDE_BATT_ID; //0x100
  outFrame.len = 8;
  outFrame.ext = false;

  outFrame.data[0] = 0x00; // Not Used
  outFrame.data[1] = 0x00; // Not Used
  outFrame.data[2] = 0x00; // Not Used
  outFrame.data[3] = 0x00; // Not Used
  //ZombieVerter, which charges at real chargers, announces the maximum as target plus 40V rather
  //than a separately configured number, and a reference constant of 200.
  uint16_t maxVolts = force09 ? carStatus.targetVoltage + 40 : settings.maxChargeVoltage;
  outFrame.data[4] = lowByte(maxVolts);
  outFrame.data[5] = highByte(maxVolts);
  //CHAdeMO 1.0 fixes this field at 100 percent; the real state of charge goes out in 0x102.
  outFrame.data[6] = force09 ? 200 : 100;
  outFrame.data[7] = 0; //not used

  logFrameOnChange(outFrame);
  ACAN_ESP32::can.tryToSend(outFrame);
  if (settings.debuggingLevel > 1)
  {
    Serial.print(F("CAR: Absolute MAX Voltage:"));
    Serial.print(settings.maxChargeVoltage);
    Serial.print(F(" Pack size: "));
    Serial.print(settings.packSizeKWH);
    timestamp();
  }


}

void CHADEMO::sendCANChargingTime()
{
  CANMessage outFrame;
  outFrame.id = CARSIDE_CHARGETIME_ID; //0x101
  outFrame.len = 8;
  outFrame.ext = false;

  outFrame.data[0] = 0x00; // Not Used
  outFrame.data[1] = 0xFF; //not using 10 second increment mode
  //Same source: 254 minutes allowed and no estimate at all, rather than our 90 and 60.
  outFrame.data[2] = force09 ? 254 : 90; //ask for how long of a charge? It will be forceably stopped if we hit this time
  outFrame.data[3] = force09 ? 0 : 60; //how long we think the charge will actually take
  outFrame.data[4] = 0; //not used
  outFrame.data[5] = 0x00; //reserved, and a strict charger may reject a nonzero value
  outFrame.data[6] = 0x0; //not used
  outFrame.data[7] = 0; //not used
  logFrameOnChange(outFrame);
  ACAN_ESP32::can.tryToSend(outFrame);
  if (settings.debuggingLevel > 1)
  {
    Serial.print(F("CAR: Charging Time"));
    timestamp();
  }


}

void CHADEMO::setStateOfCharge(uint8_t stateofcharge) {
  soc = stateofcharge;
}

void CHADEMO::sendCANStatus()
{
  uint8_t faults = 0;
  uint8_t status = 0;
  CANMessage outFrame ;

  outFrame.id = CARSIDE_CONTROL_ID; //0x102
  outFrame.len = 8;
  outFrame.ext = false;

  if (carStatus.battOverTemp) faults |= CARSIDE_FAULT_OVERT;
  if (carStatus.battOverVolt) faults |= CARSIDE_FAULT_OVERV;
  if (carStatus.battUnderVolt) faults |= CARSIDE_FAULT_UNDERV;
  if (carStatus.currDeviation) faults |= CARSIDE_FAULT_CURR;
  if (carStatus.voltDeviation) faults |= CARSIDE_FAULT_VOLTM;

  if (carStatus.chargingEnabled) status |= CARSIDE_STATUS_CHARGE;
  if (carStatus.notParked) status |= CARSIDE_STATUS_NOTPARK;
  if (carStatus.chargingFault) status |= CARSIDE_STATUS_MALFUN;
  if (bChademo10Protocol)
  {
    if (carStatus.contactorOpen) status |= CARSIDE_STATUS_CONTOP;
    if (carStatus.stopRequest) status |= CARSIDE_STATUS_CHSTOP;
  }

  if (bChademo10Protocol)	outFrame.data[0] = 2; //tell EVSE we are talking 1.0 protocol
  else outFrame.data[0] = 1; //talking 0.9 protocol
  outFrame.data[1] = lowByte(carStatus.targetVoltage);
  outFrame.data[2] = highByte(carStatus.targetVoltage);
  outFrame.data[3] = askingAmps;
  outFrame.data[4] = faults;
  outFrame.data[5] = status;
  if(settings.useBms) {
      outFrame.data[6] = soc; //charged rate (change to 100 for use with BMS SoC)
  } else {
      float chargedRate = settings.capacity > 0 ? ((settings.capacity - settings.ampHours) / settings.capacity) * 100.0f : 0.0f;
      if (chargedRate < 0.0f) chargedRate = 0.0f;
      if (chargedRate > 100.0f) chargedRate = 100.0f;
      outFrame.data[6] = (uint8_t)chargedRate; //charged rate (change to 100 for use with BMS SoC)
  }
  outFrame.data[7] = 0; //not used
  logFrameOnChange(outFrame);
  ACAN_ESP32::can.tryToSend(outFrame);

  if (settings.debuggingLevel > 1)
  {
    Serial.print(F("CAR: Protocol:"));
    Serial.print(outFrame.data[0]);
    Serial.print(F(" Target Voltage: "));
    Serial.print(carStatus.targetVoltage);
    Serial.print(F(" Current Command: "));
    Serial.print(askingAmps);
    Serial.print(F(" Target Amps: "));
    Serial.print(carStatus.targetCurrent);
    Serial.print(F(" Faults: "));
    Serial.print(faults, BIN);
    Serial.print(F(" Status: "));
    Serial.print(status, BIN);
    Serial.print(F(" kWh: "));
    Serial.print(settings.kiloWattHours);
    timestamp();


  }


  if (chademoState == RUNNING && askingAmps < carStatus.targetCurrent)
  {
    if (askingAmps == 0) askingAmps = 3;
    int offsetError = askingAmps - evse_status.presentCurrent;
    if ((offsetError <= 1) || (evse_status.presentCurrent == 0)) askingAmps++;
  }
  //not a typo. We're allowed to change requested amps by +/- 20A per second. We send the above frame every 100ms so a single
  //increment means we can ramp up 10A per second. But, we want to ramp down quickly if there is a problem so do two which
  //gives us -20A per second.
  if (chademoState != RUNNING && askingAmps > 0) askingAmps--;
  if (askingAmps > carStatus.targetCurrent) askingAmps--;
  if (askingAmps > carStatus.targetCurrent) askingAmps--;

}
