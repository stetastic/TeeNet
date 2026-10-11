# TeeNet 1.20 · Prüfung

Geprüfter Stand vom 11. Oktober 2026.

## Korrekturen

- Wolkenpuffer und Hausakku-Unterstützung bei wechselndem PV-Überschuss, Akku-Laden und zusätzlichen Hauslasten verbessert.
- Timer und Einstellungen nach Neustarts wiederhergestellt; unterbrochene PV-Erholung verlängert das Pufferbudget nicht.
- Phasenumschaltung verlangt neue Strommessungen nach dem Ladestopp und am Ende der Stillstandsprüfung.
- Ladeplan, evcc und MQTT berücksichtigen ausstehende Neustarts und das Speichern von Regelparametern.
- Vor OTA oder Neustart wird die Phasenumschaltung bei laufendem Stromfluss zurückgestellt.

## Ergebnis

Alle C- und JavaScript-Testreihen bestanden. Zusätzlich: 31 geschlossene Energieprofile, acht vollständige simulierte Tage und 27 ioBroker-Adaptertests.

| Profil | Build | Firmwaregröße |
| --- | --- | ---: |
| ESP32-S3 N16R8 | `114a8b075452` | 1.985.552 Bytes |
| Andreas Testboard | `9fb71432fe63` | 1.985.584 Bytes |

Beide Builds ohne Compilerwarnungen oder -fehler. Die Downloadpakete enthalten den zugehörigen Quellcode und die Prüfsummen.

Am N16R8 wurden OTA, einphasiger Ladestart und anschließender Neustart auf dem funktionsgleichen Vorabstand 1.19 praktisch geprüft. Vor dem Rückschalten wurden 9,86 Sekunden durchgehender Stromstillstand beobachtet; eingestellt waren mindestens acht Sekunden. Nach 22,08 Sekunden war der ESP wieder mit aktuellen Messwerten bereit. Einstellungen, Ladesitzungen und historische Kosten blieben erhalten. Version 1.20 unterscheidet sich durch die Versionsnummer und den Versionshinweis in der Anleitung; die Regelung ist unverändert.

## Grenzen

Die Ergebnisse gelten für die geprüften Fälle und den vorhandenen Aufbau. Das Andreas Testboard, andere Fahrzeuge, Shelly-/EM24-Hardware und transparente Netzwerk-Gateways benötigen eigene praktische Tests. Der frühere reale 15-Minuten-Wolkenpuffertest wurde bei diesem Build nicht wiederholt; die aktuelle Pufferprüfung erfolgte in Simulationen.
