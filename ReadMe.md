# CHAdeMOSoftware

This code will help you add CHAdeMO DC fast charging to your EV, whether it is OEM or DIY.  Currently Untested

🛟 **Need help or found a bug?** Get support at [support.doodesch.de/esp32-chademo](https://support.doodesch.de/esp32-chademo).

Based on https://github.com/Isaac96/CHAdeMOSoftware, forked from https://github.com/jamiejones85/ESP32-Chademo

This project uses ElegantOTA for OTA updates. See https://randomnerdtutorials.com/esp32-ota-over-the-air-arduino/ for instructions on how to use.

## This fork

Runs on an off-the-shelf **LC-Relay-ESP32-4R-A2** relay board (ESP32-WROOM-32E, four 5 V relays)
instead of the custom ESP32-Chademo PCB, and charges changing packs rather than one fixed vehicle.

Differences to upstream:

- No MCP2515: the CHAdeMO bus runs on the internal TWAI controller through an SN65HVD230.
- No Isabellenhuette IVT shunt. Voltage, current and power are taken from the EVSE status frame
  `0x109`, the amp hour and kilowatt hour counters are integrated from those values.
- No BMS CAN input and no VCU status frame `0x354`, both lived on the removed second bus.
- WiFi runs as an access point (`ESP32-CHADEMO` / `ChadMeO1`), the web UI is the only operator interface.

**Safety:** the mismatch checks in `Chademo.cpp` now compare the charger against its own numbers.
They cannot detect a charger reporting wrong values or a welded contactor, and there is no
independent pack measurement. Charge limits come solely from the profile entered on the settings page.

### Required hardware

- CHAdeMO connector
- 2x contactors rated for the intended DC current
- LC-Relay-ESP32-4R-A2 board
- SN65HVD230 CAN transceiver
- 1x optocoupler for charge sequence signal 1
- 1k pullup resistor
- 12 V supply for the board

### Contactor coils belong on the charger's loop

The charger closes the vehicle contactors itself. Pin 2 (charge sequence signal 1) is the +12 V
source for the coils and pin 10 (charge sequence signal 2) is their ground leg, which the charger
switches when it is ready to deliver. A vehicle that powers its coils from its own supply leaves
that loop dead, and the charger stops after the insulation test.

```
   Pin 2  (+12 V from the charger)
      |
      +---> RY2 (in series, permission only) ---> coil 1 ---+
                                            ---> coil 2 ---+
                                                           |
   Pin 10 (ground leg, closed by the charger)
```

Pin 10 carries no supply of its own and must not be tied to the board ground, so signal 2 cannot be
read with the same optocoupler arrangement as signal 1: its optocoupler goes from pin 2 through the
LED to pin 10, in parallel with the coils, and conducts when the charger closes the ground leg.

Signal 1 supplies up to 2 A, which covers two 12 V coils drawing about 1 A together. Above that the
loop drives a small 12 V relay instead, which then switches the local supply to the coils.

RY2 is a disconnect in that loop, not the thing that closes the contactors: closed means the charger
may close them. It therefore closes early, right after the charge permission contact, and has to be
closed before the charger switches the ground leg. A firmware that waits for the second sequence
signal before closing it would deadlock, since the charger only switches into a circuit that is
already complete. Current is still only requested once the insulation test has run and the output
has come back down.

### Pin map

| Function | GPIO |
|---|---|
| Charge permission contact (RY1) | 32 |
| Contactor coil permission (RY2) | 33 |
| Charge sequence signal 1, optocoupler | 27 |
| Charge sequence signal 2 | 13 |
| CAN RX / TX | 16 / 17 |
| Status LED | 2 |

### Build

```
pio run              # compile
pio run -t upload    # flash over the 6 pin serial header, IO0 to GND while flashing
pio run -t uploadfs  # upload the web UI to SPIFFS
```
