# TeeNet 1.23

Quellcode für Shell Recharge Advanced 3.0 mit zwei Hardwareprofilen:

- **ESP32-S3 N16R8:** 16 MB Flash, Shell TX17/RX18, Zähler TX4/RX5.
- **Andreas Testboard · AtomS3 Lite:** 8 MB Flash, Shell TX6/RX5, Zähler TX1/RX2, Betriebsartkontakt optional auf GPIO1.

Standard-Build: `cbf0e66c7468`.

## Bauen

ESP-IDF 5.3.2 installieren und die ESP-IDF-Konsole öffnen. Im Projektordner:

    powershell -ExecutionPolicy Bypass -File tools/build_profiles.ps1

Das Skript erzeugt beide Varianten getrennt in `build` und `build-atoms3-lite`.

Am eingerichteten TeeNet unter **System & Hilfe** nach Updates suchen und die Installation dort starten. Ladung vorher stoppen.

Erstinstallation über den [USB-Webinstaller](https://teenet-demo.stetastic.chatgpt.site/install.html) oder mit `idf.py -p PORT flash` (`PORT` ersetzen).

## Einrichten

Mit **Wallbox-EMS-Setup** verbinden, `192.168.4.1` öffnen und WLAN sowie Wallbox-Zähler einrichten.

Automatische RS485-Module mit 3,3-V-Logik verwenden. Erweiterungen, Profilanschlüsse und Verdrahtung stehen in der [Anleitung](main/guide.html) und im [Anschlussplan](main/hardware-plan.svg).

## Darstellung und Wärmepumpe

Unter **System & Hilfe → Darstellung** Standard oder Grafisch und die Anordnung Kompakt wählen.
Eine Wärmepumpe optional unter **Funktionen** aktivieren und ihren eigenen Shelly-Zähler oder MQTT-Eingang einrichten. TeeNet zeigt ihren elektrischen Verbrauch; die Wärmepumpe wird nicht gesteuert.

## Externe Steuerung

Optional evcc unter **Funktionen → Einbindung in Evcc** aktivieren. Die passende Konfiguration am Gerät herunterladen. Der separate Wallbox-Zähler bleibt erforderlich.

## Nutzung & Haftung

Privates, kostenloses Bastelprojekt. Nutzung auf eigene Gefahr.
Vor Installation und Betrieb [Nutzung, Sicherheit und Haftung](DISCLAIMER.md) lesen.
