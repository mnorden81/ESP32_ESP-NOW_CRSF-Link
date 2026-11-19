# Versionsgeschichte ESP32_ESP-NOW_CRSF-Link

Bis v0.1.9 „Funk-Brücke“, Prüfstand im Repo RC-CRSF-Router-ESP32-S3 (`pruefstand/Funk_Bruecke/`). Neueste Version oben.

## v0.2.0
- Eigenes Projekt und Repo „ESP32_ESP-NOW_CRSF-Link“. Sketch `firmware/ESP32_ESP-NOW_CRSF-Link/`.
- WLAN-Name „CRSF-Link-XXXX“ bzw. „Link-<Name>“ (vorher „Funk-Bruecke-XXXX“ / „Funk-<Name>“).
- Ping meldet zusätzlich die Rolle „Brücke“ (RC_Common 1.5.0). Multiswitch ab v5.01 und CRSF-Router ab v0.4.0 zeigen den Link so an. Gegenstellen mit v0.1.x werden weiter erkannt.
- Einstellungen aus v0.1.x bleiben erhalten (NVS `funkbr`).

## v0.1.9 (auf Hardware getestet 19.11.2026)
- Direkte Zustellung (Unicast) an bekannte Gegenstellen mit Quittung und Wiederholung durch den Funkchip, Rundruf nur ohne bekannte Gegenstelle.
- Name je Brücke (WebUI, WLAN-Name, Gegenstellen-Tabelle), Anzeige „am Empfänger“ / „am Modul“.
- Sendeleistung nach WLAN-Start erneut setzen. Neustart-Erkennung der Gegenstelle robuster.

## v0.1.8
- Verzögerung beseitigt: USB-Ausgabe blockiert nicht mehr ohne seriellen Monitor, veraltete Kanal-Frames werden bei Rückstau verworfen, UART-Sendepuffer, Zähler in der WebUI.

## v0.1.7
- Eigene MAC richtig lesen (war 00:00:…): WLAN-Name je Brücke verschieden, Laufzeitmessung funktioniert.

## v0.1.6
- Pull-up am CRSF-RX, damit ein offener Pin keine Störungen als Daten liefert.

## v0.1.5
- UART pro loop()-Durchlauf begrenzt (die Brücke am Empfänger hing sonst im Lesen fest).

## v0.1.4
- LED-Reihenfolge Standard GRB auch beim S3-Zero.

## v0.1.3
- WLAN-Blitz blau länger und heller.

## v0.1.2
- Neustart der Gegenstelle erkennen: Paketnummer neu synchronisieren.

## v0.1.1
- C3: BOOT-Taste ist GPIO9.

## v0.1.0
- Erster Prüfstand: durchsichtige Brücke, Messwerte, WebUI.
