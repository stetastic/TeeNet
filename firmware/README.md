# TeeNet 1.24

Quellcode für Shell Recharge Advanced 3.0 mit zwei Hardwareprofilen:

- **ESP32-S3 N16R8:** 16 MB Flash, Shell TX17/RX18, Zähler TX4/RX5.
- **AtomS3 Lite + Tail485:** 8 MB Flash, Shell TX2/RX1. Versorgung und RS485 über A/B/+/- der Wallbox.

Standard-Build: `7f345fd06193`.

## Bauen

ESP-IDF 5.3.2 installieren und die ESP-IDF-Konsole öffnen. Im Projektordner:

    powershell -ExecutionPolicy Bypass -File tools/build_profiles.ps1

Das Skript erzeugt beide Varianten getrennt in `build` und `build-atoms3-lite`.

Am eingerichteten TeeNet unter **System & Hilfe** nach Updates suchen und die Installation dort starten. Ladung vorher stoppen.

Erstinstallation über den [USB-Webinstaller](https://teenet-demo.stetastic.chatgpt.site/install.html) oder mit `idf.py -p PORT flash` (`PORT` ersetzen).

## Einrichten

Mit **Wallbox-EMS-Setup** verbinden, `192.168.4.1` öffnen und WLAN und Shell-Verbindung einrichten; externen Zähler oder HTTP-Regelung auswählen.

Automatische RS485-Module mit 3,3-V-Logik verwenden. Erweiterungen, Profilanschlüsse und Verdrahtung stehen in der [Anleitung](main/guide.html) und im [Anschlussplan](main/hardware-plan.svg).

## Laderegelung

Unter **Wallbox-Regelung & Zähler** zwischen externem Zähler und experimenteller HTTP-Regelung wählen. HTTP wurde mit Mercedes GLB 250 erprobt. Ladetests mit weiteren Fahrzeugen ohne Shelly-Zähler stehen aus. Strom und Leistung werden geschätzt. Standard-Untergrenze dreiphasig: 5,6 kW. Phasenwechsel setzen eine unabhängige Stromstillstandsprüfung voraus.

## Darstellung und Wärmepumpe

Unter **System & Hilfe → Darstellung** Standard oder Grafisch und die Anordnung Kompakt wählen.
Eine Wärmepumpe optional unter **Funktionen** aktivieren und ihren eigenen Shelly-Zähler oder MQTT-Eingang einrichten. TeeNet zeigt ihren elektrischen Verbrauch; die Wärmepumpe wird nicht gesteuert.

## Externe Steuerung

Optional evcc unter **Funktionen → Einbindung in Evcc** aktivieren. Die passende Konfiguration am Gerät herunterladen. Der separate Wallbox-Zähler bleibt erforderlich.

## Nutzung & Haftung

Privates, kostenloses Bastelprojekt. Nutzung auf eigene Gefahr.
Vor Installation und Betrieb [Nutzung, Sicherheit und Haftung](DISCLAIMER.md) lesen.
