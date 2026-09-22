# Kinetos OpenEVSE V5

Eigene Firmware für **Kinetos-Wallboxen** (Power Rock, Elektronik von Bond Electronics mit
WT32-ETH01) auf Basis von **OpenEVSE v5** – mit LAN, eichrechtskonformem MID-Zähler (3 Phasen),
smart1®-Modbus-TCP, §14a EnWG, Home Assistant und TeslaMate.

> *English summary:* Kinetos went bankrupt and its wallboxes are stuck on a modified OpenEVSE
> WiFi firmware 4.1.4 that tools like evcc reject. Stock OpenEVSE loses the MID meter readings
> (0 A / 0 kWh) and needs the right board pins. This repository documents the hardware and the
> original firmware (pins, Modbus meter map, smart1 register map), provides a tool to back up the
> flash over LAN without opening the box, and builds OpenEVSE v5.1.5 with a Kinetos board layer.
> Documentation is in German; the specs tables in `docs/SPECS.md` are mostly self-explanatory.

## Inhalt

| Pfad | Inhalt |
|---|---|
| [`docs/PAPER.md`](docs/PAPER.md) | Vollständiger Bericht: Ausgangslage, Hardware, Backup ohne Löten, Analyse, neue Firmware, Stolpersteine |
| [`docs/SPECS.md`](docs/SPECS.md) | Technische Referenz: Pinbelegung, Flash-Layout, MID-Zähler, smart1-Registerkarte, Konfiguration, Schnittstellen |
| [`firmware/`](firmware/) | Kinetos-Schicht (`kinetos/`), Patch für OpenEVSE v5.1.5 (`patches/`), GUI-Erweiterung (`gui/`), `build.sh` |
| [`dumper/`](dumper/) | OTA-Dumper: sichert den kompletten Flash über LAN und schaltet danach zurück |
| [`tools/re/`](tools/re/) | Analyse-Werkzeuge (ESP-Image → ELF, Literal-Pool-Suche, Ghidra-Skripte) |
| [`tools/modbus/`](tools/modbus/) | smart1-Modbus-TCP lesen/scannen |

Die Original-Firmware von Kinetos ist **nicht** enthalten (proprietär). Mit dem Dumper sichert
jeder sein eigenes Gerät.

## Funktionen der Firmware

- LAN (LAN8720) und WLAN, aktuelle OpenEVSE-v5-API mit Claims (evcc ≥ 4.1.9 kompatibel), OCPP 1.6, MQTT
- MID-Zähler (SDM630-Registerkarte, Modbus RTU): Strom, Spannung, Leistung pro Phase, Zählerstand;
  Leistung und Energiezählung kommen aus dem Zähler statt aus der Schätzung `V × A × 3`
- smart1®-kompatibler Modbus-TCP-Server (Port 502) mit der Kinetos-Registerkarte
- §14a EnWG: Dimmung auf 4,2 kW per Klemme (Steuerbox), MQTT oder HTTP, Vorrang vor allen anderen Vorgaben
- Home-Assistant-MQTT-Discovery (23 Entitäten)
- LED-Ring: PV-Anteil als Farbe (orange → grün), WS2812FX-Animationen wählbar
- Fahrzeugdaten von TeslaMate per MQTT
- Einstellungen als eigene Seite „Kinetos“ in der OpenEVSE-Oberfläche
- Absturz-Diagnose (Panic-Trap im RTC-Speicher), Roh-Eingänge, Zählerstatistik

## Schnellstart

```bash
# 1. Backup der Original-Firmware (Box muss per LAN erreichbar sein, kein Fahrzeug laden!)
#    dumper/ bauen (PlatformIO) oder das Release-Asset nehmen, dann:
curl -u admin:PASS -F firmware=@kinetos-dumper.bin http://WALLBOX/update
curl http://WALLBOX/flash -o kinetos-full-4MB.bin     # zweimal, SHA256 vergleichen
curl http://WALLBOX/revert                            # zurück auf Kinetos

# 2. Konfiguration sichern (wird beim Umstieg teilweise nicht übernommen)
curl -u admin:PASS http://WALLBOX/config -o config-before.json

# 3. Firmware bauen (PlatformIO + Node.js) und flashen
cd firmware && ./build.sh
curl -u admin:PASS -F firmware=@out/openevse-v5.1.5-kinetos-mid.bin http://WALLBOX/update
```

Danach Web-Login, WLAN und MQTT prüfen (siehe `docs/PAPER.md`, Kap. 6.2).
Zurück zur Original-Firmware: das eigene gesicherte `app0`-Image über `/update` flashen.

## Hinweise

- Eingriffe in eine Ladestation erfolgen auf eigene Verantwortung. Die Sicherheitsfunktionen
  (RCM, GFCI, Relais-Prüfung) liegen im Controller und werden nicht verändert.
- Getestet mit genau einer Box (MID-Variante, Controller 8.2.0.PR). Andere Varianten
  (z. B. „AUEM“ mit zwei Platinen) haben andere Pins.
- Der Flash-Dump enthält WLAN-, MQTT- und Web-Passwörter im Klartext – nicht weitergeben.

## Lizenz

GPL-3.0 (wie die OpenEVSE-ESP32-Firmware). Die OpenEVSE-GUI steht unter BSD-2-Clause.
„smart1“ ist eine Marke ihres Inhabers; hier nur zur Beschreibung der Kompatibilität verwendet.
