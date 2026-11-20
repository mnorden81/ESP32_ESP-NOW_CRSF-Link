# ESP32_ESP-NOW_CRSF-Link

CRSF per Funk statt Kabel: zwei kleine ESP32 ersetzen die CRSF-Leitung zwischen ELRS-Empfänger und einem Modul (Multiswitch, Soundmodul, CRSF-Router …). Übertragen wird mit **ESP-NOW** direkt zwischen den ESP32, ohne WLAN-Router.

Autor: PiperPilot · Version 0.2.0 · bis v0.1.9 hieß das Projekt „Funk-Brücke“ (Prüfstand im Repo RC-CRSF-Router-ESP32-S3). v0.1.9 ist am 19.11.2026 auf Hardware getestet: 0,2 % Verlust, Lua-Menü und Failsafe OK.

```
ELRS-Empfänger --Kabel--> Link 1  ~~ ESP-NOW ~~>  Link 2 --Kabel--> Modul
```

## Funktionen

- **Durchsichtig:** jedes gültige CRSF-Frame vom Kabel (CRC geprüft) wird gefunkt, jedes gefunkte Frame aufs Kabel geschrieben. Kanäle, Telemetrie und das Lua-Menü des Senders laufen ohne Änderung an den Modulen.
- **Gleiche Firmware für beide Seiten**, keine Rolle einzustellen. Die WebUI zeigt automatisch „am Empfänger“ oder „am Modul“.
- **Direkte Zustellung (Unicast)** an jede bekannte Gegenstelle: der Funkchip wartet auf die Quittung und wiederholt verlorene Pakete selbst. Ein Rundruf-Ping alle 200 ms findet neue Gegenstellen.
- **Netz:** alle Links und Module mit derselben Fahrzeug-Kennung und demselben Funkkanal gehören zusammen (bis 8 Gegenstellen).
- **Failsafe:** kommt nichts mehr aus dem Funk, schreibt der Link nichts aufs Kabel. Das Modul geht in seinen eigenen Failsafe wie bei abgezogenem Kabel.
- **Messwerte** je Gegenstelle: Pakete/s, Verlust, Aussetzer > 200 ms, längste Lücke, Laufzeit, RSSI. In der WebUI und im seriellen Monitor (115200).
- Läuft auf ESP32-S3-Zero, ESP32-C3 und klassischem ESP32, auch gemischt.

## Zusammenspiel mit den Modulen

Seit **RC_Common 1.5.0** steckt dieselbe Funk-Technik direkt in den Modulen (`funk_link.*`, gleiches Paketformat):

| Modul | ab Version | Einstellung |
|---|---|---|
| RC-Multiswitch C3/S3, Multiswitch Basis/TXRX | v5.01 | RC-System „CRSF + Funk senden (Zentrale)“ oder „CRSF aus Funk (Teilnehmer)“ |
| RC-CRSF-Router-ESP32-S3 | v0.4.0 | Eingang „Kabel + Funk (Zentrale)“ oder „Funk (Teilnehmer)“ |

Ein Link passt an jede Seite. Beispiele:
- Link am Empfänger, Multiswitch als Teilnehmer: kein Kabel zum Multiswitch.
- Multiswitch oder Router als Zentrale am Empfänger, Link an einem Modul ohne Funk-Firmware (z. B. Soundmodul).
- Zwei Links: jedes Modul ohne Funk-Firmware.

Im Ping meldet sich der Link ab v0.2.0 als „Brücke“, die Module zeigen ihn so in ihrer Gegenstellen-Liste.

## Anschluss

| Board | CRSF RX | CRSF TX | LED | BOOT |
|---|---|---|---|---|
| ESP32-S3-Zero | GPIO5 | GPIO6 | GPIO21 (RGB) | GPIO0 |
| ESP32-C3 | GPIO20 | GPIO21 | GPIO8 (unbestätigt) | GPIO9 |
| ESP32 (klassisch) | GPIO16 | GPIO17 | GPIO2 | GPIO0 |

- **Link am Empfänger:** RX an TX des Empfängers, TX an RX des Empfängers, GND, 5 V.
- **Link am Modul:** RX an TX des Moduls (CRSF-Eingang), TX an RX des Moduls, GND, 5 V.
- RX und TX immer **über Kreuz**. Aufbau auf dem Breadboard: [docs/STECKPLAN.md](docs/STECKPLAN.md).

## Flashen

GitHub baut die Dateien bei jedem Push: Reiter **Actions** → „Firmware bauen“ → neuester Lauf → unten bei **Artifacts** `esp-now-crsf-link-s3`, `-c3` oder `-esp32`.

- `ESP32_ESP-NOW_CRSF-Link_xx_komplett.bin` per USB ab Adresse **0x0** flashen (z. B. mit dem Espressif Web-Flasher).
- `ESP32_ESP-NOW_CRSF-Link_xx_ota.bin` ist für ein späteres Update.
- Arduino IDE: `firmware/ESP32_ESP-NOW_CRSF-Link/ESP32_ESP-NOW_CRSF-Link.ino` öffnen. Board „ESP32S3 Dev Module“, „ESP32C3 Dev Module“ (beide USB CDC On Boot **an**) oder „ESP32 Dev Module“. esp32-Core 3.x, keine weiteren Bibliotheken.

Ein Update von der Funk-Brücke v0.1.x behält alle Einstellungen.

## Bedienung

- WLAN „CRSF-Link-XXXX“ (mit Namen „Link-<Name>“), Passwort `123456789`, Seite **http://192.168.1.1**. Das WLAN läuft auf dem Funkkanal.
- **BOOT 2 s halten** = WLAN an/aus.

| LED | Bedeutung |
|---|---|
| grün blinkt langsam | Funk-Verbindung da, Kanäle kommen |
| grün blinkt schnell | Funk-Verbindung da, aber keine Kanäle |
| rot blinkt | keine Funk-Verbindung |
| kurz blau alle 2 s | WLAN ist an |

Sind Rot und Grün vertauscht, in der WebUI „LED-Reihenfolge“ umstellen (Standard GRB).

**Einstellungen** (auf allen Teilen gleich): Fahrzeug-Kennung 1–255 (Standard 1), Funkkanal 1–13 (Standard 1), Sendeleistung 2–20 dBm (Standard 8, im Modell besser 20), CRSF-Baudrate (Standard 420000), „Kanäle höchstens alle … ms“ (Standard 0 = jedes Frame), Name, LED-Reihenfolge, WLAN beim Start.

## Testablauf

1. **Grundtest:** beide Links an, Empfänger gebunden. Link 2 zeigt „Funk-Verbindung da“, Kanal-Frames aus Funk etwa so viele wie die ELRS-Paketrate.
2. **Modul** an Link 2: Kanäle reagieren ohne spürbare Verzögerung.
3. **Lua-Menü:** am Sender öffnen, das Modul erscheint und lässt sich einstellen.
4. **Failsafe:** Link 1 vom Strom trennen. Link 2 blinkt rot, das Modul geht in Failsafe. Wieder einschalten, es läuft sofort weiter.
5. **Messwerte** auf der Seite von Link 2: Verlust, Aussetzer, längste Lücke, Laufzeit.
6. **ELRS-Störung:** mit 2, 8 und 20 dBm testen, Link einmal direkt neben der Empfängerantenne, einmal 20 cm entfernt.

**Gutes Ergebnis:** Verlust unter 1 %, keine Aussetzer > 200 ms, Laufzeit unter 3 ms, ELRS-LQ bleibt gleich.

## Grenzen

- Die Laufzeit misst nur den Funkweg, nicht das Kabel davor und danach.
- Bei 500 Hz belegt der Funk viel Sendezeit. „Kanäle höchstens alle 4 ms“ begrenzt das.
- Keine Verschlüsselung, nur die Fahrzeug-Kennung trennt Fahrzeuge.
- ESP-NOW und ELRS nutzen beide 2,4 GHz. Den Link nicht direkt an die Empfängerantenne legen.

## Ordner

| Ordner | Inhalt |
|---|---|
| `firmware/ESP32_ESP-NOW_CRSF-Link/` | Arduino-Sketch |
| `docs/` | Steckplan Breadboard |
| `CHANGELOG.md` | alle Versionen |

## Verwandte Projekte

RC-CRSF-Router-ESP32-S3, RC-Multiswitch-ESP32-C3/-S3, ESP32-Multiswitch-Basis_Ziege-One (alle mit RC_Common 1.5.0 `funk_link`).

## Prüfstand (WebUI und Lua im Browser)

In [`pruefstand/`](pruefstand/) liegt eine Seite, die die echte Weboberfläche und das Lua-Menü dieser Firmware im Browser zeigt (alle Varianten, Modul simuliert): [Prüfstand öffnen](https://raw.githack.com/mnorden81/ESP32_ESP-NOW_CRSF-Link/main/pruefstand/pruefstand.html). Sie wird bei jeder Firmware-Änderung neu gebaut, die GitHub-Prüfung „Prüfstand" achtet darauf.
