# Kinetos-Wallbox – technische Referenz

Sammlung aller Befunde zur Kinetos-Wallbox (Power Rock, MID-Variante) und zur Firmware
**Kinetos OpenEVSE V5**. Den Weg dorthin beschreibt [`PAPER.md`](PAPER.md).

Legende: **belegt** = im Code oder an der laufenden Box nachgewiesen · *abgeleitet* = aus der
Programmlogik geschlossen, nicht einzeln gemessen.

---

## 1. Gerät und Original-Firmware

| Merkmal | Wert |
|---|---|
| Hersteller | Kinetos Group (insolvent), Elektronik von **Bond Electronics** |
| Board | „EV Charge Control“ `EV-CC-AC3-LM-RCM-LAN-WIFI-LCD-LED-NFC-MID` (IEC 61851-1, Mode 3) |
| ESP-Modul | WT32-ETH01 (ESP32 rev 1, 2 Kerne, 4 MB Flash, LAN8720) |
| ESP-Firmware | `4.1.4`, Build-Env `W532_E_mid_rfid`, Kinetos-Fork von OpenEVSE ESP32_WiFi_V4.x |
| Toolchain | Arduino-ESP32 2.0.x, ESP-IDF v4.4.1, kompiliert am 20.04.2022 |
| Bibliotheken | ArduinoOcpp, ArduinoMongoose, LittleFS_esp32 (Modbus selbst implementiert) |
| Controller | LGT8F328P, Firmware `8.2.0.PR`, RAPI `5.2.3` |
| EVSE-ID | `DE*KIG*E<Controller-Seriennummer>` |
| Hostname | `kinetos-<letzte 4 Hex der MAC>` |
| MQTT-Standard | Basis `kinetos/<id>`, Announce `kinetos/announce/<id>`, µLM-Gruppe `kinetos/ulm_group` |
| Update-Seite | `POST /update`, multipart-Feld `firmware` (wie OpenEVSE) |

## 2. Board „EV Charge Control“ (Bond Electronics)

Komponenten: WT32-ETH01 (aufgelötet), LGT8F328P, TTL→RS485-Modul mit automatischer
Richtungsumschaltung, RCM-Modul (6 mA DC), Netzteil AM22-12W, Schütz, externer RFID-Leser PN532
(HW-147) und MID-Zähler auf der Hutschiene.

Klemmen (Beschriftung auf der Platine): `B-` `A+` (RS485) · `GND` `SDA` `SCL` `3V3` `GND` `IRQ` `RSTO`
(RFID) · `IO14` · `IO15` `CP` `CP` `COIL` `12V` · `WA fault` `MP trip` `GND` `TEST` `C+/C-` (CT).

DIP-Schalter (Aufdruck, Schalter 1–3 bzw. 1–4):

| Modus | S1 | S2 | S3 |
|---|---|---|---|
| WA | X | X | 1 |
| MP | X | X | 0 |
| RUN | 0 | 1 | X |
| PRG | 1 | 0 | X |

| Max. Strom | S1 | S2 | S3 | S4 |
|---|---|---|---|---|
| 13 A | 1 | 0 | 0 | 0 |
| 20 A | 0 | 1 | 0 | 0 |
| 32 A | 0 | 0 | 1 | 0 |
| 63 A | 0 | 0 | 0 | 1 |

Header: ISP für den LGT8F (`RST SCK MISO / GND MOSI 5V`), Header am WT32 (`MISO SCLK MOSI / GND CS 3V3`),
Jumper-Header `RX`/`TX`.

## 3. WT32-ETH01 – Pinbelegung

| GPIO | Funktion | Quelle |
|---|---|---|
| 0 | RMII-Takteingang LAN8720 (**nie als Taster verwenden!**) | Modul, belegt |
| 1 / 3 | UART0 TX/RX → RAPI zum Controller, 115200 Baud | belegt (`Serial.begin(115200)`) |
| 4 | WS2812-LED-Kette, 7 LEDs, `NEO_GRB + NEO_KHZ800` | belegt (`Adafruit_NeoPixel(7, 4, 0x52)`) |
| 14 | Klemme IO14, in Kinetos-Firmware unbenutzt → §14a-Eingang | Ruhepegel mit Pull-up: HIGH |
| 15 | Klemme IO15 (Strapping-Pin) | Ruhepegel ohne Pull: LOW |
| 16 | PHY-Power LAN8720 | Modul |
| 17 | UART2 TX → RS485 → MID-Zähler | belegt |
| 18 / 23 | MDIO / MDC LAN8720 | Modul |
| 32 / 33 | I²C SCL / SDA → PN532 | belegt (`Wire.begin(33, 32)`) |
| 35 | UART2 RX ← RS485 ← MID-Zähler | belegt |
| 39 | Taster (Eingang, Ruhepegel HIGH) | *abgeleitet* (`digitalRead(39)` im LED-Code) |

## 4. Flash-Layout (4 MB)

| Partition | Adresse | Größe | Inhalt |
|---|---|---|---|
| nvs | 0x9000 | 0x5000 | Konfiguration (Klartext-Passwörter!) |
| otadata | 0xE000 | 0x2000 | Boot-Slot-Auswahl |
| app0 | 0x10000 | 0x1E0000 | Kinetos 4.1.4 |
| app1 | 0x1F0000 | 0x1E0000 | OTA-Ziel |
| spiffs | 0x3D0000 | 0x30000 | LittleFS |

Keine Flash-Verschlüsselung, kein Secure Boot. Die App-Slots entsprechen `min_spiffs.csv` von
OpenEVSE; das Image von Kinetos OpenEVSE V5 passt (≈ 95 % eines Slots).

## 5. MID-Zähler (Modbus RTU)

| Parameter | Wert |
|---|---|
| Schnittstelle | UART2, RX 35 / TX 17, 9600 Baud, 8N1 |
| Slave-Adresse | 1 |
| Funktion | 0x04 Read Input Registers, 2 Register pro Wert |
| Format | IEEE-754-Float, Big-Endian |
| CRC | Modbus CRC16 (0xFFFF, Polynom 0xA001) |
| Kompatibel zu | Eastron SDM630 / SDM72D-M |

| Index | Register | Messwert |
|---|---|---|
| 0–2 | 0x0000 / 0x0002 / 0x0004 | Spannung L1–L3 [V] |
| 3–5 | 0x0006 / 0x0008 / 0x000A | Strom L1–L3 [A] |
| 6–8 | 0x000C / 0x000E / 0x0010 | Wirkleistung L1–L3 [W] |
| 9 | 0x0034 | Gesamt-Wirkleistung [W] |
| 10 | 0x0048 | Import-Wirkenergie [kWh] |
| 11 | 0x0156 | Gesamt-Wirkenergie [kWh] |

Die Kinetos-Firmware zählt Phasen mit I > 1 A und schickt dem Controller `$SL 1` (1 Phase) bzw.
`$SL 2` (2–3 Phasen).

## 6. smart1® – Modbus TCP

Port 502, Unit-ID 1, FC 3/4 lesen, FC 6/16 schreiben. Unbekannte Adressen → Exception 2.
32-Bit-Werte: niederwertiges Wort zuerst. Nur aktiv mit Flag-Bit 22 (`smart1_enabled`).
Original-Implementierung: mongoose + nanoMODBUS.

| Block | Adressen | Richtung |
|---|---|---|
| Status | 999–1006 | lesen |
| Messwerte | 1099–1110 (Kinetos), 1099–1111 (V5) | lesen |
| Ladevorgang Ladepunkt 1 | 1199–1227 (+1228) | lesen |
| Ladevorgang Ladepunkt 2 | 1299–1327 | lesen (bei Single leer) |
| Leistungssteuerung | 1599–1602 | lesen/schreiben |
| Stromsteuerung | 1999–2002 | lesen/schreiben |

| Register | Bedeutung |
|---|---|
| 1000 | Zustand: 0 aus/Schlaf · 2 bereit · 7 Fahrzeug verbunden · 9 lädt · 10/11 Lüftung · 12 Ladeende · 13 Fehler · 14 FI-Fehler *(abgeleitet)* |
| 1001 | Unterzustand 0/1/2/4/5/7 |
| 1002 | 1 = verfügbar |
| 1003 | 1 frei, 2/3 Freigabe (RFID) erforderlich |
| 1004 / 1005 / 1006 | max. Hardware-Strom / max. konfigurierter Strom / aktueller Sollstrom [A] |
| 1100/1101 | Gesamtleistung [W] |
| 1102/1103, 1104/1105, 1106/1107 | Leistung L1, L2, L3 [W] |
| 1108/1109 | Zählerstand Import [kWh] |
| 1110 | Mindeststrom [A] |
| 1111 | Fehlercode (Controller-Zustand 4–11) – bei Kinetos außerhalb des Adressbereichs |
| 1220/1221 | Energie Ladevorgang [Wh] |
| 1222 | Ladedauer [s] |
| 1224/1225 | Startzeit hhmmss |
| 1226/1227 | Stoppzeit hhmmss |
| 1228 | 0 kein · 1 läuft · 2 pausiert · 3 beendet |
| 1599 | Leistungs-Sollwert [W] (÷ Spannung × Phasen → A) |
| 1600 / 2000 | 1 = pausieren (während der Ladung) |
| 1601 / 2001 | 1 = freigeben · 2 = sperren (V5: 0 bei Sollwert 0 = Kontrolle zurückgeben) |
| 1602 / 2002 | Watchdog [s]: ohne neuen Sollwert → Pause |
| 1999 | Strom-Sollwert [0,01 A], ≥ 600 |

## 7. Controller-Verhalten (RAPI)

- `$GG` liefert nur Strom und Spannung einer Phase, `$G7` → `$NK` (keine Phasenumschaltung).
- Unterstützt u. a. `$GV $GE $GF $GP $GS $GI $GA $GC $G0 $G3 $GD $GH $GL $GU`.
- `$GP` Temperaturen: nur Sensor 2 geliefert.

## 8. Kinetos-Konfiguration (NVS)

Kompaktes JSON im selben Format wie OpenEVSE-ConfigJson, z. B. `ws` WLAN-SSID, `wp` WLAN-Passwort,
`lan` Sprache, `au`/`ap` Web-Login, `ms`/`mu`/`mp` MQTT-Server/-User/-Passwort, `smp` Shaper-Leistung,
`lb` LED-Helligkeit, `cc` Ladezähler, `ei` EVSE-ID, `f` Flags.

Flags der Testbox: `4195466` = Bits 1, 3, 7, 10, 22 (MQTT, SNTP, MQTT ohne Zertifikatsprüfung,
Lademodus, **smart1**). Kinetos verwendet Bit 22 für smart1, upstream OpenEVSE für
`ocpp_auto_auth`.

Zusätzliche `/status`-Felder der Kinetos-Firmware: `amp2`, `amp3` (mA), `voltage2`, `voltage3`,
`powersum`. Web-Oberfläche: Bereiche „µLM“ (Gruppen-Lastmanagement per MQTT) und „Modbus TCP“
(smart1).

## 9. Kinetos OpenEVSE V5 – Schnittstellen

Build-Env `wt32-eth01-kinetos-mid`, Version `v5.1.5-kinetos.<rev>`.

Neue Konfigurationsschlüssel (`/config`):

| Schlüssel | Standard | Bedeutung |
|---|---|---|
| `smart1_enabled` | an | smart1 Modbus TCP |
| `p14a_enabled` | aus | §14a-Steuerung |
| `p14a_pin` | 14 | Eingang (14, 15, 255 = keiner) |
| `p14a_active_high` | aus | aus: Kontakt gegen GND (Pull-up) · an: 3,3 V |
| `p14a_limit` | 4200 | Leistungsgrenze [W] (Claim-Priorität 5100, übernimmt eine niedrigere fremde Grenze) |
| `led_pv_enabled` | an | LED-Farbe nach PV-Anteil |
| `led_fx_charge` | 3 | WS2812FX-Modus beim Laden |
| `ha_discovery_enabled` | an | Home-Assistant-Discovery |

Zusätzliche `/status`-Felder: `amp2`, `amp3` (A), `voltage2`, `voltage3`, `powersum`, `phases`,
`mid_import_kwh`, `mid_total_kwh`, `mid_valid`, `mid_requests`, `mid_responses`, `mid_errors`,
`p14a_active`, `p14a_source` (0/1 Eingang/2 Fern), `p14a_limit_a`, `p14a_input`, `pv_share`,
`reset_reason`, `uptime_ms`, `last_panic`, `gpio14`, `gpio15`, `gpio39`.

MQTT (unter `<mqtt_topic>`): `p14a/set` (`1`/`0`) sowie die Werte pro Phase alle 10 s.
Home Assistant: `homeassistant/<component>/kinetos_<id>/<key>/config`.
HTTP: `POST /kinetos/p14a` (`1`/`0`).
EVSE-Claims: smart1 `0xFFFE5301` (Priorität 500), §14a `0xFFFE5302` (Priorität 5100, `max_current`).
