# CHAdeMO-Adapter, Testbuild

Firmware für ein **LC-Relay-ESP32-4R-A2** (ESP32-WROOM-32E, vier 5-V-Relais).
Sie gibt sich an einer CHAdeMO-Säule als Fahrzeug aus. Quellcode:
https://github.com/DooDesch/ESP32-Chademo, gebauter Stand siehe `BUILD-COMMIT.txt`.

**Der Code ist ungetestet.** Bitte nichts an Hochspannung anschließen, solange Schritt 1 und 2
unten nicht sauber durchgelaufen sind.

## Was du brauchst

- USB-TTL-Adapter mit 3,3 V Pegel (FT232, CP2102 oder ähnlich)
- Jumper oder Drahtbrücke für IO0 nach GND
- Versorgung für das Board: 5 V am 5-V-Eingang oder 7 bis 30 V am DC-Eingang

Verkabelung zum 6-poligen Programmierheader:

| USB-TTL | Board |
|---|---|
| GND | GND |
| TX | RX |
| RX | TX |
| 3V3 oder 5V | 5V |

Zum Flashen IO0 auf GND brücken, dann Board neu starten. Nach dem Flashen Brücke lösen und
erneut neu starten.

## Flashen

Beide Dateien sind Komplettabbilder und kommen auf Adresse **0x0**.

**Weg A, im Browser (kein Setup):** https://espressif.github.io/esptool-js/ öffnen, Connect,
Port wählen, Datei bei Adresse `0x0` eintragen, Program.

**Weg B, per Kommandozeile:**

```
pip install esptool
esptool.py --chip esp32 --port COM3 --baud 460800 write_flash 0x0 relaytest-full-flash.bin
```

## Schritt 1: Pinbelegung prüfen

`relaytest-full-flash.bin` flashen. Serielle Konsole mit **115200 Baud** öffnen.

Das Programm schaltet die Relais der Reihe nach für je 2 Sekunden und schreibt dazu, welchen
GPIO es gerade schaltet. Erwartet wird: RY1 = GPIO 32, RY2 = GPIO 33, RY3 = GPIO 25,
RY4 = GPIO 26, LED an GPIO 2 blinkt mit.

Bitte zurückmelden, ob Klick und Anzeige-LED wirklich zu dieser Reihenfolge passen. Wenn nicht,
brauche ich die tatsächliche Zuordnung, sonst schaltet die Firmware später das falsche Relais.

## Schritt 2: CHAdeMO-Firmware

`chademo-full-flash.bin` flashen. Danach:

1. Serielle Konsole 115200 Baud, das Board meldet `ESP32-CHADEMO` und die AP-IP.
2. WLAN `ESP32-CHADEMO`, Passwort `ChadMeO1`, im Browser http://192.168.4.1 öffnen.
3. Die Startseite zeigt Spannung, Strom, Leistung und den CHAdeMO-Zustand, die Seite
   Settings die Ladegrenzwerte.

Bitte melden:

- Startet der AP, ist die Weboberfläche erreichbar, laden die Anzeigen?
- Bleiben eingetragene Werte auf der Settings-Seite nach einem Neustart erhalten?
- Meldet die serielle Konsole `Can0 Configuration error`?
- Stürzt das Board im Betrieb neu (Reboot-Meldungen in der Konsole)?

## Wenn ein CAN-Transceiver angeschlossen ist

CAN-RX liegt auf GPIO 16, CAN-TX auf GPIO 17, Baudrate 500 kBit/s. Mit einem USB-CAN-Adapter am
Bus sollten zyklisch die Fahrzeugframes `0x100`, `0x101` und `0x102` auftauchen. Ein Mitschnitt
davon wäre die wichtigste Rückmeldung.

## Bekannte Einschränkungen dieses Builds

- Keine eigene Spannungs- und Strommessung. Alle Werte kommen von der Säule, die
  Abweichungsprüfungen der Firmware vergleichen die Säule also mit sich selbst.
- Kein BMS-Anschluss. Ladeschluss richtet sich allein nach den Werten auf der Settings-Seite.
- Kein Not-Aus in Hardware, nur der Stopp-Knopf in der Weboberfläche.

## Dateien

| Datei | Zweck |
|---|---|
| `relaytest-full-flash.bin` | Pintest, Adresse 0x0 |
| `chademo-full-flash.bin` | CHAdeMO-Firmware, Adresse 0x0 |
| `firmware.bin` | nur die Anwendung, Adresse 0x10000 |
| `spiffs.bin` | nur die Weboberfläche, Adresse 0x290000 |
| `bootloader.bin` | Adresse 0x1000 |
| `partitions.bin` | Adresse 0x8000 |
| `boot_app0.bin` | Adresse 0xe000 |
