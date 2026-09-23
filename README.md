# Kinetos OpenEVSE V5

Eigene Firmware für **Kinetos-Wallboxen** (Power Rock, Elektronik von Bond Electronics mit
WT32-ETH01) auf Basis von **OpenEVSE** (aktueller master) – mit LAN, MID-Zähler (3 Phasen),
smart1®-Modbus-TCP, §14a EnWG, Load Sharing, Home Assistant, WLED und TeslaMate.

> **Eichrecht:** Der MID-Zähler ist geeicht, die Box ist damit aber **nicht eichrechtskonform**
> (dafür bräuchte die ganze Station eine Zulassung und signierte Messwerte). Öffentlich pro kWh
> abrechnen darf man damit nicht.

> *English summary:* Kinetos went bankrupt and its wallboxes are stuck on a modified OpenEVSE
> WiFi firmware 4.1.4 that tools like evcc reject. Stock OpenEVSE loses the MID meter readings
> (0 A / 0 kWh) and needs the right board pins. This repository documents the hardware and the
> original firmware (pins, Modbus meter map, smart1 register map), provides a tool to back up the
> flash over LAN without opening the box, and builds current OpenEVSE master with a Kinetos board
> layer (MID meter, §14a dimming, Modbus TCP, display and LED features). Ready-made firmware is on the
> [Releases](../../releases) page; the box then offers updates from there itself.
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

- Ab Rev 9 (`v5.2.0-dev-kinetos.9`) auf Basis des aktuellen OpenEVSE-master: neue Oberfläche, Load Sharing,
  Hinweise aus den Fehlerzählern des Controllers; Einstellungen von Rev ≤ 8 werden beim ersten Start übernommen
- LAN (LAN8720) und WLAN, aktuelle OpenEVSE-v5-API mit Claims (evcc ≥ 4.1.9 kompatibel), OCPP 1.6, MQTT
- MID-Zähler (SDM630-Registerkarte, Modbus RTU): Strom, Spannung, Leistung pro Phase, Zählerstand;
  Leistung und Energiezählung kommen aus dem Zähler statt aus der Schätzung `V × A × 3`
- smart1®-kompatibler Modbus-TCP-Server (Port 502) mit der Kinetos-Registerkarte
- §14a EnWG: Dimmung auf 4,2 kW per Klemme (Steuerbox), MQTT oder HTTP, Vorrang vor allen anderen Vorgaben
- Home-Assistant-MQTT-Discovery (23 Entitäten)
- LED-Ring: PV-Anteil als Farbe (orange → grün), WS2812FX-Animationen wählbar
- Fahrzeugdaten von TeslaMate per MQTT
- Einstellungen direkt in der normalen OpenEVSE-Oberfläche (Monitoring, Energy → Grid dimming,
  Connectivity → Modbus TCP, Charger → LED, MQTT → Home Assistant, Vehicle → TeslaMate), nur Englisch
- Absturz-Diagnose (Panic-Trap im RTC-Speicher), Roh-Eingänge, Zählerstatistik
- Display wie im Original (Leistung in kW aus dem MID-Zähler, Animationen), **Starttext frei einstellbar**,
  Nachrichten aufs Display per MQTT (`<topic>/display/set`)
- Zähler-Übergabe an den Laderegler wie im Original (`$F2`/`$T0`/`$T1`) – der Regler kennt Strom und Energie
- **Ladeprotokoll** pro Ladevorgang mit RFID-Karte, Nutzer und kWh aus dem MID-Zähler (History → Charging sessions, CSV)
- RFID-Karte → TeslaMate-Auto (optional), eigene Stromgrenze bei 1-phasigem Laden
- Front-Taster: kurz = Start/Pause oder Boost, 1–2 s = Netzwerk-Info
- LED-Ring: Light Show, Nachtabsenkung 21–7 Uhr, **WLED-kompatible API** (WLED-App, HA-WLED-Integration)
- Load Sharing mit **Live-Gruppenbudget per MQTT** (z. B. aus dem Hausanschluss-Zähler)
- EVSE-ID wie im Original (`DE*KIG*E…`, änderbar) – Achtung: `DE*KIG` ist nicht (mehr) beim BDEW registriert
- **Prometheus**-Metriken unter `/metrics`
- **Updates direkt aus diesem Repo** (Settings → Firmware)
- Einstellungen werden zusätzlich im Dateisystem gesichert und bei einem Ladefehler automatisch wiederhergestellt
- Migration von Einstellungen der Original-Kinetos-Firmware und älterer Revisionen

## Schnellstart

```bash
# 1. Backup der Original-Firmware (Box muss per LAN erreichbar sein, kein Fahrzeug laden!)
#    dumper/ bauen (PlatformIO) oder das Release-Asset nehmen, dann:
curl -u admin:PASS -F firmware=@kinetos-dumper.bin http://WALLBOX/update
curl http://WALLBOX/flash -o kinetos-full-4MB.bin     # zweimal, SHA256 vergleichen
curl http://WALLBOX/revert                            # zurück auf Kinetos

# 2. Konfiguration sichern. Passwörter (WLAN, MQTT) zeigt die Box nicht an – vorher notieren!
curl -u admin:PASS http://WALLBOX/config -o config-before.json

# 3. Firmware flashen: fertige Datei von der Releases-Seite …
curl -u admin:PASS -F firmware=@kinetos-v5-v5.2.0-dev-kinetos.18.bin http://WALLBOX/update
#    … oder selbst bauen (PlatformIO + Node.js)
cd firmware && ./build-master.sh        # Ergebnis: out/kinetos-v5-<version>.bin
```

Danach Web-Login, WLAN und MQTT prüfen (siehe `docs/PAPER.md`, Kap. 6.2). Spätere Updates bietet
die Box selbst an (Settings → Firmware), sie kommen aus den Releases dieses Repos.
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
