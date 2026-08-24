# CHAdeMO-Adapter, Testbuild

Board: LC-Relay-ESP32-4R-A2. Der Code ist ungetestet - vor bestandenem Schritt 1 nichts an
Hochspannung.

## Flashen

USB-TTL 3,3 V an den 6-poligen Header: GND-GND, TX-RX, RX-TX, 5V-5V.
IO0 auf GND brücken, Board neu starten, flashen, Brücke lösen, neu starten.

Browser: https://espressif.github.io/esptool-js/, Datei auf Adresse `0x0`.

CLI: `esptool.py --chip esp32 --port COM3 --baud 460800 write_flash 0x0 <datei>`

## Schritt 1: relaytest-full-flash.bin

Serielle Konsole 115200 Baud. Die Relais schalten nacheinander für je 2 s.

Erwartet: RY1 = GPIO32, RY2 = GPIO33, RY3 = GPIO25, RY4 = GPIO26, LED an GPIO2.

Rückmeldung: stimmt die Reihenfolge?

## Schritt 2: chademo-full-flash.bin

WLAN `ESP32-CHADEMO`, Passwort `ChadMeO1`, dann http://192.168.4.1

Rückmeldung:

- Weboberfläche erreichbar, Anzeigen aktualisieren sich?
- Settings-Werte nach Neustart noch da?
- `Can0 Configuration error` in der Konsole?
- Reboots im Betrieb?

Mit CAN-Adapter am Bus (RX GPIO16, TX GPIO17, 500 kBit/s): Mitschnitt von 0x100, 0x101, 0x102.

## Einschränkungen

- Keine eigene Messung, alle Werte kommen von der Säule.
- Kein BMS, Ladeschluss nur nach den Werten der Settings-Seite.
- Not-Aus nur in der Weboberfläche.
- Jeder im WLAN kann ohne Anmeldung Schütze schalten und Firmware flashen.

## Dateien

| Datei | Adresse |
|---|---|
| `relaytest-full-flash.bin` | 0x0 |
| `chademo-full-flash.bin` | 0x0 |
| `firmware.bin` | 0x10000 |
| `spiffs.bin` | 0x290000 |
| `bootloader.bin` | 0x1000 |
| `partitions.bin` | 0x8000 |
| `boot_app0.bin` | 0xe000 |
