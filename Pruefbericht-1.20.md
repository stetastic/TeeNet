# TeeNet 1.20 · Prüfung

Geprüfter Stand vom 16. Oktober 2026.

## Änderungen

- Phasenumschaltung über GPIO-Relais, Shelly HTTP oder Shelly Modbus TCP; die Rückmeldung kommt bei Shelly über dessen Eingänge.
- Ein oder zwei Rückmeldekontakte werden unterstützt. Betrieb ohne Rückmeldung und der manuelle Schütztest liegen geschützt im Expertenmodus.
- Frei wählbare GPIO-Belegung mit Prüfung auf reservierte und doppelt belegte Pins.
- Einrichtung der Phasenumschaltung ist ohne vorhandene Messwerte möglich. Geschaltet wird weiterhin nur bei gestoppter Ladung und bestätigtem Stromstillstand.
- Xemex-Wallbox-Zähler unterstützt eine oder zwei Messspulen zusätzlich zur dreiphasigen Messung.
- Startprüfung und vorbereitetes OTA-Rollback; bestehende Installationen benötigen dafür einmalig den neuen Bootloader per USB.
- Weboberfläche nur noch komprimiert eingebettet. Dadurch bleibt deutlich mehr Platz im OTA-Bereich.
- Anleitung nur an den geänderten Stellen zu Schütz, Shelly, Rückmeldung und Expertenfunktionen aktualisiert.

## Ergebnis

Die vollständigen C- und JavaScript-Prüfungen bestanden, darunter Phasenlogik, Rückmeldekontakte, Shelly HTTP/Modbus, MQTT-Abgrenzung, GPIO-Zuordnung, Einstellungen, Speichergrenzen und beide Hardwareprofile. Zusätzlich bestanden 31 geschlossene Energieprofile mit acht simulierten Tagen sowie 27 ioBroker-Adaptertests.

| Profil | Build | Firmwaregröße | frei im OTA-Bereich |
| --- | --- | ---: | ---: |
| ESP32-S3 N16R8 | `54e7f2a46c4a` | 1.699.680 Bytes | 397.472 Bytes |
| Andreas Testboard | `4f9be5676d4c` | 1.699.760 Bytes | 397.392 Bytes |

Beide Firmwareprofile wurden mit ESP-IDF 5.3.2 vollständig gebaut. Firmware, USB-Pakete, Factory-Images, Quellcode und Prüfsummen wurden gegeneinander geprüft.

## Grenzen

Die neue Shelly-Schützanbindung ist softwareseitig geprüft, jedoch noch nicht an realer Shelly- und Schützhardware erprobt. Das Andreas Testboard bleibt ebenfalls ein Hardwaretestprofil. Vor dem Einsatz der Phasenumschaltung müssen Schütz, Rückmeldekontakte und sichere Ruhestellung am eigenen Aufbau geprüft werden.
