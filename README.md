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
| [`firmware/`](firmware/) | Patches für OpenEVSE-master (`patches-master/`, gebaut mit `build-master.sh`); alt: Kinetos-Schicht (`kinetos/`), Patch für v5.1.5 (`patches/`), GUI (`gui/`), `build.sh` |
| [`dumper/`](dumper/) | OTA-Dumper: sichert den kompletten Flash über LAN und schaltet danach zurück |
| [`tools/re/`](tools/re/) | Analyse-Werkzeuge (ESP-Image → ELF, Literal-Pool-Suche, Ghidra-Skripte) |
| [`tools/modbus/`](tools/modbus/) | smart1-Modbus-TCP lesen/scannen |

Die Original-Firmware von Kinetos ist **nicht** enthalten (proprietär). Mit dem Dumper sichert
jeder sein eigenes Gerät.

## Funktionen der Firmware

- Ab Rev 9 (`v5.2.0-dev-kinetos.9`) auf Basis des aktuellen OpenEVSE-master: neue Oberfläche, Load Sharing,
  Hinweise aus den Fehlerzählern des Controllers; Einstellungen von Rev ≤ 8 werden beim ersten Start übernommen
- LAN (LAN8720) und WLAN, aktuelle OpenEVSE-v5-API mit Claims (evcc ≥ 4.1.9 kompatibel), OCPP 1.6, MQTT
- MID-Zähler (Modbus RTU): Strom, Spannung, Leistung pro Phase, Zählerstand; Leistung und Energiezählung
  kommen aus dem Zähler statt aus der Schätzung `V × A × 3`. Ab Rev 20 werden beide verbauten Zählertypen
  erkannt (SDM630-kompatibel und DTS4921) samt Parität 8N1/8E1, siehe unten
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

## Zählervarianten (ab Rev 20)

In Kinetos-Boxen stecken zwei verschiedene MID-Zähler:

| Zähler | Registerkarte | Format | Werkseinstellung |
|---|---|---|---|
| SDM630-kompatibel (Eastron-Karte) | 0x0000 ff. | IEEE-754-Float | 9600 Baud, 8N1 |
| DTS4921 | 0x0100 ff. (eigene Karte) | uint32/int32, feste Skalierung | Adresse 1, 9600 Baud, **8E1** |

Bis Rev 18 las die Firmware nur die SDM630-Karte; ein DTS4921 lieferte damit Unsinn
(z. B. `voltage: 1.6e-33`, Issue #4). Jetzt erkennt der Treiber den Zähler beim Start und nach
10 s ohne Antwort neu:

1. L1-Spannung mit der SDM630-Karte lesen, dann mit der DTS4921-Karte. Die erste Karte, die
   100–300 V liefert, bleibt. Ein unplausibler Wert oder eine Modbus-Fehlerantwort heißt: andere Karte.
2. Antwortet der Zähler dreimal nicht (Timeout oder CRC-Fehler), wird die andere Parität probiert
   (8N1 ↔ 8E1). Die zuletzt funktionierende Einstellung kommt beim nächsten Mal zuerst.
3. `/status` zeigt `meter_type` (`sdm630`, `dts4921`, `unknown`) und `meter_format` (`8N1`, `8E1`;
   in Rev 20 hieß das Feld `meter_serial`). Spannungen werden auf 0,1 V, Ströme und kWh auf 0,01 gerundet
   (ab Rev 21). Alle anderen Felder und Einheiten bleiben gleich.

Die Firmware **schreibt nie** in den Zähler. Beim DTS4921 setzt ein Schreibzugriff auf 0x0020 den
Zählerstand auf null, 0x0061–0x0063 ändern Adresse, Baudrate und Parität. Register: `docs/SPECS.md`, Kap. 5.

Stand: Die SDM630-Erkennung ist der bisherige Weg. Der DTS4921-Weg ist gegen die Herstellerdoku
geprüft (Host-Test mit deren Beispielantwort, 238,1 V) und wurde mit Rev 20 an einem echten DTS4921
erkannt (Issue #2: `dts4921`, 8N1). Rückmeldungen mit `meter_type`, `meter_format` und `meter_errors`
sind willkommen.

### Zählerwerte an den Regler (`$T0`/`$T1`)

Wie die Original-Firmware schaltet die Firmware den Kinetos-Regler (Firmware-Hauptversion 8, gemeldet z. B. als
`8.2.0.PR` oder `E532_L-8.2.0` – letzteres erst ab Rev 22 erkannt) mit `$F2 0` auf externe
Messwerte und schickt ihm dann alle 2 s die Phasenströme (`$T0 <mA L1> <mA L2> <mA L3>`) und bei jeder
Änderung den Zählerstand (`$T1 <Wh>`) – auch ohne Ladevorgang. Bis Rev 20 gab die Firmware auf, wenn der
Regler `$F2 0` in den ersten 6 Sekunden ablehnte (z. B. im Zustand „disabled“). Ab Rev 21 versucht sie es
jede Minute weiter, wiederholt `$F2 0` alle 10 Minuten und nach einem abgelehnten `$T0`. `/status` zeigt
`controller_meter_feed`, `controller_meter_feed_state` (`on`, `enabling`, `f2_rejected`, `f2_no_answer`,
`t0_rejected`, `meter_invalid`, `controller_not_8x`), `controller_meter_feed_answer` (letzte Antwort auf
`$F2 0`: 0 OK, 1 NK, -1 Timeout) und `controller_firmware`.

## EEBUS-Image (ab Rev 19, noch nicht auf einer Box getestet)

`build-master.sh` baut zwei Images aus denselben Quellen, weil OCPP und EEBUS nicht zusammen in die 4 MB passen:
`kinetos-v5-<version>.bin` (mit OCPP) und `kinetos-eebus-v5-<version>.bin` (EEBUS statt OCPP). EEBUS
(openeebus von NIBE): Wallbox als steuerbare Verbrauchseinrichtung (LPC, §14a-Grenze in Watt von Steuerbox
oder Energiemanager) und Messstelle (MPC); Einstellungen unter Settings → EEBUS. Die Update-Prüfung wählt
das Release-Asset nach dem Image-Typ.

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
curl -u admin:PASS -F firmware=@kinetos-v5-<version>.bin http://WALLBOX/update
#    … oder selbst bauen (PlatformIO + Node.js)
cd firmware && ./build-master.sh        # Ergebnis: out/kinetos-v5-<version>.bin, out/kinetos-eebus-v5-<version>.bin
```

Danach Web-Login, WLAN und MQTT prüfen (siehe `docs/PAPER.md`, Kap. 6.2). Spätere Updates bietet
die Box selbst an (Settings → Firmware), sie kommen aus den Releases dieses Repos.
Zurück zur Original-Firmware: das eigene gesicherte `app0`-Image über `/update` flashen.

## Hinweise

- Eingriffe in eine Ladestation erfolgen auf eigene Verantwortung. Die Sicherheitsfunktionen
  (RCM, GFCI, Relais-Prüfung) liegen im Controller und werden nicht verändert.
- Getestet mit genau einer Box (MID-Variante mit SDM630-kompatiblem Zähler, Controller 8.2.0.PR).
  Andere Varianten (z. B. „AUEM“ mit zwei Platinen) haben andere Pins.
- Der Flash-Dump enthält WLAN-, MQTT- und Web-Passwörter im Klartext – nicht weitergeben.

## Lizenz

GPL-3.0 (wie die OpenEVSE-ESP32-Firmware). Die OpenEVSE-GUI steht unter BSD-2-Clause.
„smart1“ ist eine Marke ihres Inhabers; hier nur zur Beschreibung der Kompatibilität verwendet.
