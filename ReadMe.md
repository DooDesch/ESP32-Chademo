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
- 2x optocoupler for the charge sequence signals
- 1k pullup resistor
- 12 V supply for board and contactor coils

### Pin map

| Function | GPIO |
|---|---|
| Charge permission contact (RY1) | 32 |
| Contactor coils (RY2) | 33 |
| Charge sequence signal 1, optocoupler | 34 |
| Charge sequence signal 2, optocoupler | 35 |
| CAN RX / TX | 16 / 17 |
| Status LED | 2 |

### Build

```
pio run              # compile
pio run -t upload    # flash over the 6 pin serial header, IO0 to GND while flashing
pio run -t uploadfs  # upload the web UI to SPIFFS
```
