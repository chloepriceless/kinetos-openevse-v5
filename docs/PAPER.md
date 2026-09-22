# Kinetos OpenEVSE V5 – Kinetos-Wallbox ohne Hersteller: Firmware sichern, analysieren und auf aktuelles OpenEVSE bringen

*Arbeitsstand: 23.09.2026 · Status: Firmware `v5.1.5-kinetos.6` läuft auf der Box, evcc steuert sie über die Claims-API*

> **Kurzfassung.** Kinetos-Wallboxen (Kinetos Group, insolvent) basieren auf OpenEVSE, laufen aber
> mit einer eingefrorenen, angepassten ESP32-Firmware (4.1.4). Aktuelle Software wie evcc verlangt
> mindestens OpenEVSE-WiFi-Firmware 4.1.9. Wer die Standard-Firmware flasht, verliert bei den
> MID-Varianten die Messwerte (0 A / 0 kWh), weil Kinetos den eichrechtskonformen Zähler über ein
> eigenes Modbus-Modul ausliest. Dieses Paper zeigt, wie man **ohne Öffnen und ohne Löten** ein
> vollständiges Flash-Backup über LAN zieht, was in der Kinetos-Firmware steckt, und wie daraus eine
> aktuelle OpenEVSE-Firmware mit LAN, MID-Zähler (3 Phasen), Modbus-TCP und RFID entsteht.

---

## 1. Ausgangslage

| | |
|---|---|
| Gerät | Kinetos Wallbox (Power Rock), 3-phasig, MID-Zähler, RFID, LAN + WLAN |
| ESP-Firmware | `4.1.4`, Build-Env `W532_E_mid_rfid` (Kinetos-Fork von OpenEVSE ESP32_WiFi_V4.x) |
| Controller | `8.2.0.PR`, RAPI `5.2.3` (LGT8F328P) |
| Problem | evcc: „WiFi-Firmware zu alt“ – das evcc-Template `openevse` verlangt **≥ 4.1.9** (Claims-API) |
| Hersteller | Kinetos Group insolvent, keine Updates, Quellcode nie veröffentlicht |

Das `W532` im Build-Namen steht für das ESP-Modul **WT32-ETH01** (ESP32 + LAN8720-Ethernet).

## 2. Hardware

Die Elektronik stammt nicht von Kinetos selbst, sondern von **Bond Electronics**:
Board „EV Charge Control“ **EV-CC-AC3-LM-RCM-LAN-WIFI-LCD-LED-NFC-MID** (IEC 61851-1, Mode 3).

Auf dem Board (Fotos aus dem OpenEnergyMonitor-Forum, siehe Quellen):

- **WT32-ETH01** (ESP32, LAN8720 intern: MDC=GPIO23, MDIO=GPIO18, PHY-Adresse 1,
  Takt über GPIO0, PHY-Power GPIO16) – aufgelötet
- **LGT8F328P** als EVSE-Controller (OpenEVSE-kompatibel, RAPI), ISP-Header vorhanden
- **TTL→RS485-Modul** (blau, automatische Richtungsumschaltung) an Klemmen `A+`/`B-` → MID-Zähler
- **RFID-Leser PN532** (Modul HW-147) über I²C (Klemmen `SDA`, `SCL`, `IRQ`, `RSTO`)
- Klemmen `IO14`, `IO15`, `CP`, `COIL`, `12V`, `WA fault`, `MP trip`, `TEST`, `C+/C-` (CT)
- DIP-Schalter mit den Modi `WA` / `MP` / `RUN` / `PRG` und Stromstufen 13/20/32/63 A
- Jumper-Header `RX`/`TX` (Programmierzugang)
- **RCM-Modul** (Fehlerstrom-Erkennung, 6 mA DC), Schütz, AM22-12W-Netzteil

Es gibt mindestens eine zweite Variante („AUEM“, zwei Platinen: ESP32 + ATmega), für die ein
Community-Fork existiert (`pohlinkzei/openevse_esp32_firmware`, Branch `kinetos-auem`).

## 3. Was Kinetos zusätzlich eingebaut hat

Durch Analyse der Web-Oberfläche und der API der laufenden Box (nur lesend):

| Funktion | Beschreibung | Upstream OpenEVSE? |
|---|---|---|
| MID-Zähler | Modbus RTU über RS485, liefert Strom und Spannung L1–L3 sowie Leistung Σ (`amp2`, `amp3`, `voltage2`, `voltage3`, `powersum` in `/status`) | nein |
| smart1® | Modbus-TCP-Server auf Port 502 (Unit 1), Registerblöcke 999–1006, 1099–1110, 1199–1227, 1299–1327, 1599–1602, 1999–2002 | nein |
| µLM | Micro-Lastmanagement über MQTT-Gruppe (`kinetos/ulm_group`) | nein |
| LAN | Ethernet über WT32-ETH01 | **ja** (`ENABLE_WIRED_ETHERNET`) |
| RFID | PN532 über I²C | **ja** (`ENABLE_PN532`) |

Der Controller selbst liefert per RAPI **keine** Werte pro Phase (`$GG` = ein Strom, eine Spannung;
`$G7` → `$NK`, also keine Phasenumschaltung). Die 3-Phasen-Werte kommen ausschließlich vom
MID-Zähler, den der ESP32 ausliest. **Deshalb zeigt Standard-OpenEVSE auf MID-Boxen 0 A / 0 kWh.**

## 4. Backup ohne Löten: der OTA-Dumper

OpenEVSE bietet keine Funktion, um den Flash über das Netzwerk auszulesen. Klassisch bräuchte man
einen USB-Seriell-Adapter am WT32-ETH01. Es geht aber auch per LAN:

**Prinzip.** Der ESP32 hat zwei App-Slots (`app0`, `app1`). Ein Update über `/update` schreibt
immer in den *gerade nicht laufenden* Slot, die bisherige Firmware bleibt unangetastet. Wir flashen
deshalb ein kleines Programm („Dumper“), das

1. mit denselben Ethernet-Parametern wie das WT32-ETH01 startet,
2. den kompletten Flash (4 MB) per HTTP ausliefert (`GET /flash`),
3. Partitionen und Versionsinfos anzeigt (`GET /info`),
4. per `GET /revert` den Boot-Slot wieder auf die Kinetos-Firmware setzt und neu startet,
5. **automatisch nach 20 Minuten** auf Kinetos zurückschaltet (Sicherheitsnetz) und
6. ohne LAN ein Notfall-WLAN `kinetos-dumper` öffnet.

Quellcode: [`dumper/src/main.cpp`](../dumper/src/main.cpp) (PlatformIO, `espressif32@6.12.0`, Board `wt32-eth01`).

**Ablauf (real durchgeführt am 22.09.2026):**

```bash
# 0. Nur wenn kein Fahrzeug lädt!  Konfiguration sichern:
curl -u admin:PASS http://WALLBOX/config -o config-before.json
# 1. Dumper per Kinetos-Update-Seite flashen
curl -u admin:PASS -F firmware=@kinetos-dumper.bin http://WALLBOX/update
# 2. Info und Dump (zweimal, Hashes vergleichen)
curl http://WALLBOX/info
curl http://WALLBOX/flash -o kinetos-full-4MB.bin
# 3. Zurück auf Kinetos
curl http://WALLBOX/revert
```

Ergebnis:

- Partitionstabelle: `nvs` 0x9000 (20 KB), `otadata` 0xE000, `app0` 0x10000 (0x1E0000),
  `app1` 0x1F0000 (0x1E0000), `spiffs` 0x3D0000 (192 KB). Keine Flash-Verschlüsselung.
- Kinetos-App in `app0`: ESP-IDF v4.4.1, kompiliert am 20.04.2022, Prüfsumme und SHA256 gültig.
- Zwei Downloads bitidentisch (SHA256 `a327c56a…`).
- Nach `/revert` lief die Box wieder unverändert auf Kinetos 4.1.4 (LAN, MQTT, MID-Werte ok).

> ⚠️ Der NVS-Bereich (0x9000–0xE000) enthält WLAN-, MQTT- und Web-Passwörter im Klartext.
> Dumps nie ungeprüft weitergeben – für das Repo wird NVS mit 0xFF überschrieben.

## 5. Analyse der Kinetos-Firmware

Werkzeuge: `esptool image-info`, eigenes [`tools/re/img2elf.py`](../tools/re/img2elf.py) (ESP-App-Image → ELF mit
einem Segment pro Load-Bereich), Ghidra 12.1 headless (Prozessor `Xtensa:LE:32:default`) und das
Exportskript [`tools/re/scripts/ExportDecomp.java`](../tools/re/scripts/ExportDecomp.java) (7028 Funktionen
dekompiliert). Konstanten liegen bei Xtensa in Literal-Pools, deshalb hilft
[`tools/re/findlit.py`](../tools/re/findlit.py): Es sucht einen Wert (z. B. `SERIAL_8N1` = `0x800001c`) im Code und
nennt die Pool-Adresse, die Ghidra als `DAT_xxxxxxxx` anzeigt.

Basis der Kinetos-Firmware: OpenEVSE ESP32_WiFi_V4.x (~4.1.x), ArduinoOcpp, ArduinoMongoose, LittleFS,
kompiliert mit Arduino-ESP32 2.0.x / ESP-IDF 4.4.1. Das Modbus-Protokoll ist **selbst implementiert**.

### 5.1 MID-Zähler (Modbus RTU)

| Parameter | Wert | Beleg |
|---|---|---|
| UART | `Serial2.begin(9600, SERIAL_8N1, RX=GPIO35, TX=GPIO17)` | `FUN_400d3100` |
| Slave-Adresse | 1 | Request-Header `01 04` in `FUN_400d3340` |
| Funktion | 0x04 (Read Input Registers), 2 Register je Wert | `FUN_400d3340` |
| Datenformat | IEEE-754-Float, Big-Endian (Wort- und Byte-Reihenfolge) | Byte-Umordnung in `FUN_400d3384` |
| CRC | Modbus CRC16 (Init 0xFFFF, Polynom 0xA001) | `FUN_400d330c` |
| Richtungsumschaltung | keine (RS485-Modul schaltet automatisch) | kein DE-Pin im Code |

Registertabelle (`0x3ffbdb68`, 12 Einträge à 12 Byte: Register, Skalierung 1.0, Messwert):

| Index | Register | Bedeutung | Status-Feld |
|---|---|---|---|
| 0 / 1 / 2 | 0x0000 / 0x0002 / 0x0004 | Spannung L1 / L2 / L3 [V] | `voltage` / `voltage2` / `voltage3` |
| 3 / 4 / 5 | 0x0006 / 0x0008 / 0x000A | Strom L1 / L2 / L3 [A] | `amp` / `amp2` / `amp3` (×1000) |
| 6 / 7 / 8 | 0x000C / 0x000E / 0x0010 | Wirkleistung L1 / L2 / L3 [W] | – |
| 9 | 0x0034 | Gesamt-Wirkleistung [W] | `powersum` |
| 10 | 0x0048 | Import-Wirkenergie [kWh] | – |
| 11 | 0x0156 | Gesamt-Wirkenergie [kWh] | – |

Das ist exakt die Registerkarte der **Eastron SDM630 / SDM72D-M** (bzw. kompatibler Zähler).

**Phasenerkennung:** Die Kinetos-Firmware zählt die Phasen mit Strom > 1 A und schickt dem
Controller `$SL 1` (einphasig) bzw. `$SL 2`, damit dessen Leistungsberechnung stimmt (`FUN_400d3404`).

### 5.2 smart1® (Modbus TCP, Port 502)

Server: mongoose-Listener `tcp://0.0.0.0:502` + Bibliothek **nanoMODBUS**, Unit-ID 1, FC 3/4 (lesen),
FC 6/16 (schreiben). Aktiv nur mit `smart1_enabled` (Flag-Bit 22). Unbekannte Adressen → Exception 2.
32-Bit-Werte: **niederwertiges Wort zuerst**. Quelle: `FUN_400d4b78` (Adressmap), `FUN_400d4f60`
(Lesewerte), `FUN_400d4de0` (Schreibwerte).

Lesen (Auszug, verifiziert gegen die laufende Box):

| Register | Inhalt |
|---|---|
| 1000 | Ladepunkt-Zustand: 0 aus/Schlaf, 2 bereit (kein Fahrzeug), 7 Fahrzeug verbunden, 9 lädt, 10/11 Lüftung angefordert, 12 Ladeende (Fahrzeug noch verbunden), 13 Fehler, 14 FI-Fehler *(Bedeutung aus der Zustandslogik abgeleitet)* |
| 1001 | Unterzustand (0/2/4/5/7) |
| 1002 | verfügbar (1) / Fehler (2) |
| 1003 | 1 = frei, 2/3 = RFID-/Freigabe-Status |
| 1004 / 1005 | Stromgrenzen des Controllers [A] |
| 1006 / 1007 | aktueller Ladestrom-Sollwert [A] |
| 1100/1101 | Gesamtleistung [W] (32 Bit) |
| 1102/1103 … 1106/1107 | Leistung L1 / L2 / L3 [W] |
| 1108/1109 | Import-Energie [kWh] (MID-Register 0x0048) |
| 1110 | Mindeststrom [A] |
| 1111 | Fehlercode (Controller-Zustand ≥ 4) |
| 1220/1221 | Session-Energie [Wh] |
| 1222 | Ladedauer [s] |
| 1224/1225 | Startzeit (hhmmss, z. B. 121527 = 12:15:27) |
| 1226/1227 | Stoppzeit (hhmmss) |
| 1228 | Session-Zustand (1 läuft, 2/3 beendet) |
| 1299–1327 | zweiter Ladepunkt (Twin/Quattro), bei Single leer |

Schreiben:

| Register | Inhalt |
|---|---|
| 1599 | Leistungs-Sollwert [W] → wird über die Spannung in A umgerechnet |
| 1600 | 1 = pausieren (nur wenn gerade geladen wird) |
| 1601 | 1 = freigeben, 2 = sperren |
| 1602 | Timeout [s]: nach Ablauf ohne neuen Sollwert → pausieren (Watchdog) |
| 1999 | Strom-Sollwert [0,01 A], gültig ab 600 (= 6,00 A) |
| 2000 / 2001 / 2002 | wie 1600 / 1601 / 1602 |

### 5.3 Weitere Pins (WT32-ETH01)

| Funktion | GPIO | Beleg |
|---|---|---|
| RAPI zum Controller (LGT8F) | UART0 (GPIO1 TX / GPIO3 RX), 115200 Baud | `Serial.begin(115200)` |
| MID-Zähler RS485 | RX 35, TX 17 | siehe oben |
| I²C (RFID PN532) | SDA 33, SCL 32 | `Wire.begin(0x21, 0x20)` in `FUN_400ea150` |
| LED-Kette (WS2812/NeoPixel) | GPIO4, 7 LEDs, `NEO_GRB + NEO_KHZ800` | `FUN_40120de4(obj, 7, 4, 0x52)` |
| Eingang (Taster/Schlüsselschalter, **ungeprüft**) | GPIO39 | `digitalRead(0x27)` |
| Ethernet LAN8720 | MDC 23, MDIO 18, Takt GPIO0, Power 16, PHY-Adr. 1 | Modul-Standard |

Zum Vergleich der Community-Fork für die AUEM-Variante: RAPI auf `Serial`, NeoPixel GPIO4 ×7. Das
bestätigt die gemeinsame WT32-Grundbelegung beider Varianten.

## 6. Neue Firmware: OpenEVSE 5.x + Kinetos-Hardware-Schicht

*Stand: `v5.1.5-kinetos.2` läuft seit 22.09.2026 auf der Test-Box (LAN, RAPI, MID, smart1, MQTT, HA, §14a geprüft).*

Basis ist aktuelles OpenEVSE **v5.1.5**. Das bringt aktuelle APIs, Claims (die evcc nutzt), OCPP 1.6
über MicroOcpp, MQTT und die neue Weboberfläche. Die Kinetos-Anpassung ist bewusst klein und
getrennt gehalten (`firmware/`):

| Datei | Inhalt |
|---|---|
| `kinetos/kinetos_meter.*` | Nicht blockierender Modbus-RTU-Treiber für den MID-Zähler (12 SDM630-Register reihum, CRC, Timeout, Gültigkeit 10 s) |
| `kinetos/kinetos_smart1.*` | smart1®-kompatibler Modbus-TCP-Server (Port 502, FC 3/4/6/16) mit Kinetos-Registerkarte; Steuerbefehle laufen über einen eigenen EVSE-Claim |
| `patches/0001-kinetos-board-support.patch` | Build-Env `wt32-eth01-kinetos-mid`, Konfig-Flag `smart1_enabled`, Einbindung der Zählerwerte in Status, Leistung und Energiezählung, Debug-Ausgabe ohne UART |
| `build.sh` | Klont upstream, wendet den Patch an und baut `out/openevse-v5.1.5-kinetos-mid.bin` |

Verhalten:

- **Messwerte:** Ist der MID-Zähler erreichbar, kommen `amp`/`voltage` (L1) und `power` (Σ L1–L3) aus
  dem Zähler statt aus dem Einphasen-Stromwandler des Controllers. Die Energiezählung integriert die
  echte Gesamtleistung, statt `V × A × 3` zu schätzen. Das ist bei 1- oder 2-phasig ladenden Autos
  deutlich genauer.
- **Status-API:** Die Kinetos-Felder `amp2`, `amp3`, `voltage2`, `voltage3` und `powersum` bleiben
  erhalten, damit bestehende Integrationen weiterlaufen. Neu: `phases`, `mid_import_kwh`,
  `mid_total_kwh`, `mid_valid` und Zähler-Statistik.
- **smart1:** Die Registerkarte ist wie bei Kinetos. Die Sollwerte wirken als Claim mit API-Priorität,
  dadurch sieht evcc sie und kann sie übersteuern. Der Watchdog pausiert wie im Original.
  Erweiterung: Setzt man alle Steuerregister auf 0, wird der Claim wieder freigegeben.
- **Pins:** RAPI auf UART0, LEDs GPIO4 ×7 (WS2812FX), PN532 an I²C 33/32, LAN8720.
  Taster/GPIO39 wird nicht belegt, solange die Funktion ungeprüft ist.

Bewusst **nicht** übernommen: das Umschalten des Controller-Service-Levels (`$SL 1/2`) je nach
Phasenzahl. Die Leistung kommt jetzt ohnehin direkt vom Zähler.


### 6.1 Stolperstein: GPIO0 als „WLAN-Taster“ (Absturzschleife)

Der erste Flash lief nur wenige Sekunden und startete dann in einer Schleife neu (`reset_reason` 4 =
Panic). Die Absturzmeldung landet auf UART0 und damit beim Controller, war also unsichtbar. Abhilfe
war eine **Absturz-Falle**: Der Panic-Handler wird per Linker-Option `-Wl,--wrap=esp_panic_handler`
umgeleitet und legt Ursache und Backtrace im RTC-Speicher ab, der den Neustart überlebt
(`kinetos/kinetos_crash.cpp`). `/status` liefert das dann als `last_panic`. Das Dekodieren mit
`xtensa-esp32-elf-addr2line` ergab:

`operator new` → `LcdTask::display` → `NetManagerTask::displayState` → `NetManagerTask::serviceButton`

Ohne eigene Definition nimmt upstream **GPIO0 als Taster**. Beim WT32-ETH01 ist GPIO0 aber der
50-MHz-RMII-Takteingang. Die Button-Logik sieht deshalb ständig „gedrückt/losgelassen“, flutet die
LCD-Warteschlange bis zum Speicherüberlauf und hätte bei 10 s „gedrückt“ sogar einen Werksreset
ausgelöst. Lösung: Tasterlogik abschalten (`KINETOS_BUTTON_DISABLED`). Die Kinetos-Firmware liest den
Taster an **GPIO39** (Ruhepegel HIGH); das Aktivieren folgt nach einem Drucktest.

### 6.2 Stolperstein: Konfiguration beim Umstieg

v5 übernimmt beim ersten Start die Kinetos-Konfiguration (gleiches kompaktes JSON-Format, z. B.
`{"ws":…, "au":…, "ms":…}`). Beim zweiten Firmware-Wechsel gingen die übernommenen Werte (WLAN,
MQTT, **Web-Login**) aber verloren. Danach war die Box kurzzeitig ohne Passwort erreichbar. Spätere
Updates behalten die Konfiguration (getestet). **Empfehlung:** vor dem Umstieg `/config` sichern, nach
dem Umstieg Web-Login, MQTT und WLAN prüfen und bei Bedarf neu setzen.

### 6.3 Neue Funktionen

| Funktion | Beschreibung | Konfiguration |
|---|---|---|
| **§14a EnWG** | Netzorientierte Steuerung: Bei aktivem Signal wird die Ladeleistung auf `p14a_limit` (Standard 4200 W) begrenzt. Der Strom wird aus der Zahl der aktiven Phasen laut MID-Zähler berechnet (3-phasig → 6 A). Umgesetzt als `max_current`-Claim (Priorität 1100), der evcc, smart1 und alle anderen Claims übersteuert. Quellen: potentialfreier Kontakt an Klemme IO14 (gegen GND, interner Pull-up, 1 s Entprellung) oder MQTT `<topic>/p14a/set` = `1`/`0`. | `p14a_enabled`, `p14a_pin` (14), `p14a_active_high`, `p14a_limit` |
| **Home Assistant** | MQTT-Discovery beim Verbinden: 23 Entitäten (Leistung, Ströme/Spannungen L1–L3, Energie, MID-Zählerstand, Zustand, Fahrzeug-SoC/Reichweite, Schalter „Laden erlaubt“, Ladestrom-Regler, §14a-Status und -Fernsteuerung). | `ha_discovery_enabled` |
| **LED-Ring PV-Anteil** | Beim Laden wird von Orange (Netz) nach Grün (PV) gemischt, auf Basis von `mqtt_grid_ie` oder `mqtt_solar`. Die Animationsgeschwindigkeit folgt der Leistung, der Effekt ist aus allen WS2812FX-Modi wählbar (3 Wipe, 12 Regenbogen, 18 Lauflicht, 43 Larson, 44 Komet …). | `led_pv_enabled`, `led_fx_charge` |
| **Fahrzeugdaten über TeslaMate** | SoC, Reichweite und Restladezeit kommen per MQTT von TeslaMate (`teslamate/cars/1/…`). TeslaMates Restladezeit in Stunden wird in Sekunden umgerechnet. Der eingebaute Tesla-Client (alte Owner-API) ist abgeschaltet. | `vehicle_data_src=2`, `mqtt_vehicle_*` |
| **Werte pro Phase per MQTT** | `amp2/3`, `voltage2/3`, `powersum`, `phases`, `mid_import_kwh` alle 10 s als Event (MQTT + Websocket). | – |
| **GUI-Seite „Kinetos“** | Eigener Menüpunkt in der OpenEVSE-Oberfläche (Konfiguration → Kinetos). Die GUI (`gui-v2`, Svelte) wird dafür aus dem Quellcode neu gebaut (`firmware/gui/apply.py`). Inhalt: MID-Livewerte, §14a (Klemme, Polarität, Grenze, Testknopf), LED/PV-Quelle und Animation, smart1/HA/3-phasig, TeslaMate-Übernahme per Knopf, Diagnose. Entfernt: OhmConnect-Menü, Tesla-Datenquelle, FR/ES/HU. `POST /kinetos/p14a` (`1`/`0`) steuert §14a per HTTP. | – |
| **PV-Anteil nur mit lebender Quelle** | Die LED-Farbe zählt nur, wenn in den letzten 5 Minuten ein Netz- oder PV-Messwert per MQTT kam. Sonst würde ein ungenutztes Standard-Topic (`emon/emonpi/power1`) als „0 W Netzbezug“ gelten und fälschlich 100 % PV anzeigen. | – |
| **Spannung vom MID-Zähler** | `getVoltage()` liefert bei gültigem Zähler die gemessene L1-Spannung statt der fest eingestellten Controller-Spannung (240 V). | – |
| **Diagnose** | `reset_reason`, `uptime_ms`, `last_panic` und die Roh-Eingänge `gpio14/15/39` in `/status`. | – |
| **Schlanker** | Web-UI-Sprachen FR/ES/HU entfernt (−23 KB). Trotz der neuen Funktionen ist das Image kleiner (95,1 % statt 95,9 % des App-Slots). Die v5-GUI hat kein Deutsch (nur EN), das wäre ein eigener GUI-Build. | – |

**Lastmanagement:** µLM war Kinetos-proprietär, andere Hersteller (z. B. go-e) sprechen es nicht.
Universell nutzbar und bereits in v5 enthalten sind der **Current Shaper** (Hausanschluss-Schutz anhand
der Haus-Gesamtleistung per MQTT, `current_shaper_enabled`, `mqtt_live_pwr`,
`current_shaper_max_pwr`), **OCPP 1.6 Smart Charging** (`SetChargingProfile` über MicroOcpp, für
Backends wie SteVe/CitrineOS mit Gruppen-Lastmanagement) und **evcc** (Stromkreise/„circuits“ über
mehrere Wallboxen). Dazu kommt smart1/Modbus TCP für externe EMS.

## 7. Ausblick: Fahrzeugerkennung („Plug & Charge light“)

Beim normalen AC-Laden nach IEC 61851 gibt es **keine digitale Kommunikation** mit dem Fahrzeug.
Die Wallbox sieht nur die PWM am CP-Kontakt und die Spannungsstufen A–F. Eine Seriennummer oder VIN
wird nicht übertragen, auch nicht an eine Kinetos-Box. Der Tesla Wall Connector zeigt die VIN nur bei
Tesla-Fahrzeugen an. Das läuft über Teslas eigene Kommunikation und lässt sich nicht nachbauen.

Realistische Wege:

1. **RFID pro Fahrzeug:** Heute schon möglich. Der PN532 liest die Karte, OpenEVSE 5.x meldet
   `rfid_auth`, und evcc ordnet darüber das Fahrzeug zu.
2. **ISO 15118 / EVCCID („Autocharge“):** Mit einem HomePlug-Green-PHY-Modem (z. B. QCA7000/7005,
   angekoppelt an CP) kann die Wallbox bei Fahrzeugen, die auf AC die digitale Kommunikation
   (5 % Duty Cycle) unterstützen, die **EVCCID**, also die MAC-Adresse des Fahrzeugmodems,
   auslesen. Sie ist pro Fahrzeug stabil und taugt als Kennung. Das braucht Zusatz-Hardware, SPI-Pins
   am WT32 und eine SLAC/V2G-Implementierung (Vorbild: pyPLC im EVSE-Modus). Das ist ein
   eigenständiges Folgeprojekt.

## 8. Rechtliches und Sicherheit

- Die Kinetos-Firmware ist proprietär. **Veröffentlicht werden nur Methode, Analyseergebnisse und
  eigener Code**, nicht das Kinetos-Binary.
- Eingriffe in eine Ladestation erfolgen auf eigenes Risiko. Die Sicherheitsfunktionen (RCM, GFCI,
  Relais-Prüfung) liegen im Controller (LGT8F); der wird hier nicht verändert.
- Eichrecht: Der MID-Zähler bleibt physisch unverändert. Die Abrechnungskette (Transparenz-Software,
  signierte Messwerte) ist bei Kinetos ohnehin nicht mehr gegeben.

## Quellen

- OpenEnergyMonitor-Forum: [Kinetos professional openEVSE Wallbox issues](https://community.openenergymonitor.org/t/kinetos-professional-openevse-wallbox-issues/28762)
- OpenEVSE Support: [Kinetos-Thread](https://openev.freshdesk.com/support/discussions/topics/6000071030)
- [OpenEVSE ESP32-Firmware](https://github.com/OpenEVSE/ESP32_WiFi_V4.x), [Wired Ethernet](https://github.com/OpenEVSE/openevse_esp32_firmware/blob/master/docs/wired-ethernet.md)
- [pohlinkzei/openevse_esp32_firmware @ kinetos-auem](https://github.com/pohlinkzei/openevse_esp32_firmware/tree/kinetos-auem)
- evcc: [Template `openevse`](https://github.com/evcc-io/evcc/blob/master/templates/definition/charger/openevse.yaml) (Anforderung ≥ 4.1.9)
