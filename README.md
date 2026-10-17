# TeeNet 1.21

Lokale ESP32-S3-Steuerung für die **Shell Recharge Advanced 3.0**.

[Online-Demo](https://stetastic.github.io/TeeNet/) · [Bebilderte Anleitung](https://stetastic.github.io/TeeNet/help) · [Downloads](https://stetastic.github.io/TeeNet/downloads) · [USB-Webinstaller](https://stetastic.github.io/TeeNet/install.html)

## Funktionen

- Ladeleistung manuell einstellen oder automatisch mit PV-Überschuss laden.
- Über den ioBroker-Adapter oder Home Assistant per MQTT steuern.
- Optional evcc für die Ladesteuerung verwenden.
- PV- und Akkudaten von OpenDTU-OnBattery per MQTT nutzen.
- Hausakku einbeziehen und PV-Leistung zwischen Hausakku und Auto aufteilen.
- Optional Ladeplan, Wolkenpuffer und automatische Phasenumschaltung über GPIO oder Shelly nutzen.
- Verbrauch, Ladesitzungen und Netz-/Solarkosten anzeigen und als CSV exportieren.

Einrichtung und Bedienung erfolgen im Browser. Die Online-Demo lässt sich ohne Hardware ausprobieren.

## Installation

Neue Geräte über den [USB-Webinstaller](https://stetastic.github.io/TeeNet/install.html) installieren; dabei werden vorhandene Daten gelöscht. Eingerichtete Geräte unter **System & Hilfe** direkt von GitHub aktualisieren. Dort lässt sich vorher eine vollständige Systemsicherung herunterladen. Firmware und Quellcode stehen bei den [Veröffentlichungen](https://github.com/stetastic/TeeNet/releases/latest).

## Hardware

Empfohlen wird der erprobte **ESP32-S3 N16R8 mit 16 MB Flash** und USB-Versorgung. Ein automatischer RS485-Wandler mit 3,3-V-Logik verbindet ihn mit der Shell. Für einen RS485-Wallbox-Zähler wird ein zweiter Wandler benötigt.

Für Andreas steht zusätzlich das Profil **Andreas Testboard · AtomS3 Lite** bereit. Es nutzt 8 MB Flash, Shell TX6/RX5 und optional GPIO1 für den Betriebsartkontakt. Diese Variante ist als Hardwaretest gekennzeichnet.

Ein **separater Zähler am Wallbox-Abgang** ist für die Regelung erforderlich. Die Shell muss für externes dynamisches Lastmanagement eingerichtet sein. Unterstützte Zähler und Anschlüsse beschreibt die [Anleitung](https://stetastic.github.io/TeeNet/help).

Am Referenzaufbau erprobt: dreiphasig 6–11 kW, einphasig 2,0–3,5 kW. Der nutzbare Bereich hängt von Fahrzeug und Aufbau ab. Details stehen im [Messprotokoll](https://github.com/stetastic/TeeNet/blob/main/Probelauf.md).

## Freiwillige Gerätestatistik

Mit dem aktuellen Build von Version 1.21 kann TeeNet nach ausdrücklicher Bestätigung eine zufällige Kennung und die Firmwareversion an den Projektzähler senden. Die Teilnahme ist bei der Einrichtung vorausgewählt und jederzeit abschaltbar. Keine Lade-, WLAN- oder Standortdaten. Die Demo sendet keine Lebenszeichen. Details stehen in der Anleitung unter Gerätestatistik.

## Hinweis

**Privates, kostenloses Bastelprojekt. Nutzung auf eigene Gefahr.** Netzanschlüsse nur durch Elektrofachkräfte. Eingriffe in die Wallbox können Garantieansprüche beeinträchtigen. Vor Installation und Betrieb [Nutzung, Sicherheit und Haftung](DISCLAIMER.md) lesen.



Die Geräteoberfläche prüft beim Öffnen und anschließend alle sechs Stunden auf passende neue Firmware. Bei einer neueren Version erscheint ein Hinweis mit „Update ansehen“ und „Später“. Die Installation wird ausdrücklich bestätigt.
