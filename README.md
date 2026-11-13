# TeeNet 1.24

Lokale Ladesteuerung für die Shell Recharge Advanced 3.0: manuelles Laden, PV-Überschuss, Hausakku, Fahrzeugprofile und Ladepläne. Bedienung im Browser; ioBroker, Home Assistant und MQTT sind optional.

[Demo](https://stetastic.github.io/TeeNet/) · [Anleitung](https://stetastic.github.io/TeeNet/help.html) · [Downloads & USB-Installation](https://stetastic.github.io/TeeNet/downloads.html) · [Version 1.24](https://github.com/stetastic/TeeNet/releases/tag/v1.24)

## Hardware

| Board | Flash | Shell-RS485 |
| --- | --- | --- |
| M5Stack AtomS3 Lite + Tail485 | 8 MB | TX GPIO2 / RX GPIO1 |
| ESP32-S3 N16R8 | 16 MB | TX GPIO17 / RX GPIO18 |

AtomS3 Lite und N16R8 sind reguläre Hardwareprofile. Bestehende Atom-Installationen erhalten weiterhin das passende Update. Tail485 verbindet A, B, + und − mit dem passenden Wallbox-Anschluss. Anschlussbelegung und Versorgung vor der Installation anhand der Anleitung prüfen.

Optional: Shelly 3EM als Wallbox-Zähler, Hausanschlusszähler für PV-Laden und ein über Netzwerk angesteuertes Phasenschütz. Die Anleitung beschreibt Öffner-/Schließerkontakte und die optionale Stellungsprüfung über Phasenspannungen.

## Neu in 1.24

- HTTP-Regelung ohne externen Wallbox-Zähler ist dauerhaft auswählbar; sie läuft selbstständig im ESP.
- Mit Mercedes GLB 250 getestet. Ladetests mit weiteren Fahrzeugen ohne Shelly-Zähler stehen aus; andere Netzwerklatenzen sind nicht bestätigt.
- Neue dreiphasige Standard-Untergrenze: 5,6 kW. Die früheren Auswahlschalter für 5,5 und 6 kW entfallen. Individuelle Expertenparameter bleiben erhalten.
- Kürzere Einstellungen, zusammengeführte Downloads für beide Boards und überarbeitete Anleitung.

Bei HTTP-Regelung werden Strom und Leistung geschätzt. Die kWh-Abrechnung nutzt die Zählerstände der Wallbox. Fehlende HTTP-Daten stoppen die Regelung; ein Phasenwechsel benötigt weiterhin einen unabhängigen Nachweis des Stromstillstands.

## Installieren

Vor einem Update eine Systemsicherung herunterladen. Auf bestehenden Geräten die OTA-Datei für das eigene Board unter **System & Hilfe** installieren. Eine USB-Erstinstallation ist für neue Geräte vorgesehen und kann vorhandene Daten löschen.

Der Quellcode liegt unter [firmware](firmware/). Die passende Anschlussanleitung liegt unter [firmware/main/guide.html](firmware/main/guide.html).

## Nutzung

Privates, kostenloses Projekt. Hinweise zu Installation und Betrieb stehen in der [Anleitung](https://stetastic.github.io/TeeNet/help.html). Netzanschlüsse gehören in die Hände einer Elektrofachkraft.
