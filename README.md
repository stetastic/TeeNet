# TeeNet 1.19

Lokale ESP32-S3-Steuerung für die **Shell Recharge Advanced 3.0**.

[Online-Demo](https://teennet-demo.stetastic.chatgpt.site/) · [Bebilderte Anleitung](https://teennet-demo.stetastic.chatgpt.site/help) · [Downloads](https://teennet-demo.stetastic.chatgpt.site/downloads) · [USB-Webinstaller](https://teennet-demo.stetastic.chatgpt.site/install.html)

## Funktionen

- Ladeleistung manuell einstellen oder automatisch mit PV-Überschuss laden.
- Über den ioBroker-Adapter oder Home Assistant per MQTT steuern.
- Optional evcc für die Ladesteuerung verwenden.
- PV- und Akkudaten von OpenDTU-OnBattery per MQTT nutzen.
- Hausakku einbeziehen und PV-Leistung zwischen Hausakku und Auto aufteilen.
- Optional Ladeplan, Wolkenpuffer und automatische Phasenumschaltung nutzen.
- Verbrauch, Ladesitzungen und Netz-/Solarkosten anzeigen und als CSV exportieren.

Einrichtung und Bedienung erfolgen im Browser. Die Online-Demo lässt sich ohne Hardware ausprobieren.

## Installation

Neue Geräte über den [USB-Webinstaller](https://teennet-demo.stetastic.chatgpt.site/install.html) installieren; dabei werden vorhandene Daten gelöscht. Eingerichtete Geräte unter **System & Hilfe** direkt von GitHub aktualisieren. Dort lässt sich vorher eine vollständige Systemsicherung herunterladen. Firmware und Quellcode stehen bei den [Veröffentlichungen](https://github.com/stetastic/TeeNet/releases/latest).

## Hardware

Empfohlen wird der erprobte **ESP32-S3 N16R8 mit 16 MB Flash** und USB-Versorgung. Ein automatischer RS485-Wandler mit 3,3-V-Logik verbindet ihn mit der Shell. Für einen RS485-Wallbox-Zähler wird ein zweiter Wandler benötigt.

Für Andreas steht zusätzlich das Profil **Andreas Testboard · AtomS3 Lite** bereit. Es nutzt 8 MB Flash, Shell TX6/RX5 und optional GPIO1 für den Betriebsartkontakt. Diese Variante ist als Hardwaretest gekennzeichnet.

Ein **separater Zähler am Wallbox-Abgang** ist für die Regelung erforderlich. Die Shell muss für externes dynamisches Lastmanagement eingerichtet sein. Unterstützte Zähler und Anschlüsse beschreibt die [Anleitung](https://teennet-demo.stetastic.chatgpt.site/help).

Am Referenzaufbau erprobt: dreiphasig 6–11 kW, einphasig 2,0–3,5 kW. Der nutzbare Bereich hängt von Fahrzeug und Aufbau ab. Details stehen im [Messprotokoll](https://github.com/stetastic/TeeNet/blob/main/Probelauf.md).

## Hinweis

**Privates, kostenloses Bastelprojekt. Nutzung auf eigene Gefahr.** Netzanschlüsse nur durch Elektrofachkräfte. Eingriffe in die Wallbox können Garantieansprüche beeinträchtigen. Vor Installation und Betrieb [Nutzung, Sicherheit und Haftung](DISCLAIMER.md) lesen.
