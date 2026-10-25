# TeeNet 1.23: Prüfung bestehender Anlagen

Verglichen wurden der gesicherte funktionierende Quellstand 1.19 (Build 277d15efb407) und die veröffentlichte 1.23. Die Korrekturen behalten Versionsnummer und Einstellungsformat 34 bei. Das ist eine Prüfung mit konkreten Nachweisen, keine Zusicherung vollständiger Fehlerfreiheit.

## Bestätigte Fehler und Korrekturen

| Punkt | Bisheriges Verhalten | Korrigierter Stand |
|---|---|---|
| Relaispolarität | Gespeichertes LOW-aktiv wurde beim Laden und Speichern auf HIGH-aktiv geändert; der GPIO-Treiber setzte HIGH fest voraus. Eine unveränderte Altanlage konnte dadurch in der falschen Stellung stehen. | HIGH bleibt der Standard neuer Anlagen. Gespeicherte LOW-Schaltlogik bleibt erhalten; Startpegel und Pull-Widerstand passen zur Polarität. Änderung nur mit gestoppter Ladung und passender Stromprüfung. |
| Einzelner Rückmeldekontakt | Die Migration älterer Formate setzte „geschlossen = einphasig“ pauschal zurück. Eine gültige Stellung konnte danach umgekehrt angezeigt werden. | Bei einem Kontakt bleibt die Zuordnung erhalten. Zwei komplementäre Kontakte behalten ihre festen Rollen. |
| Konfiguration bei Rollback | Unterschiedliche Einstellungsformate überschrieben denselben alten NVS-Schlüssel. Eine ältere Firmware konnte einen neueren Datensatz ablehnen und mit Grundwerten starten. | Neue Datensätze erhalten einen Schlüssel je Format. Der Loader sucht absteigend nach gespeicherten Formaten. Vorhandene ältere Datensätze werden nicht überschrieben. Bereits früher überschriebene Daten lassen sich damit nicht nachträglich rekonstruieren; dafür braucht es eine Sicherung. |
| OTA-Sperre | Die Prüfung schloss nur einen frischen Messwert ab 1 A aus. Veraltete/ungültige Daten und ein laufender Schütztest waren damit nicht vollständig abgedeckt. | Datei- und GitHub-Installation verwenden dieselbe Sperre für Ladung, Test, Bewegung, Wartung und veraltete/ungültige Stromwerte. Die Wiederherstellung einer noch nie vermessenen Erstinstallation bleibt möglich. |

## Was erhalten geblieben ist

- Kein bisheriger normaler Konfigurationsschlüssel wurde gegenüber dem Vergleichsstand entfernt. Das Problem lag insbesondere im Überschreiben gespeicherter Werte.
- Keine numerischen Standardwerte der Experten-Regelparameter wurden in diesem Vergleich geändert. Ein Fahrzeugstatus-Parameter bekam eine andere Gruppe und Beschreibung.
- 17 gezielt verglichene Kernfunktionen für PV-/Akkuleistung, Grenzwerte, Energieberechnung, Zählerdekodierung und Timer-Speicherung sind unverändert. Der PV-Regler erhielt zusätzlich eine sofort wirksame Begrenzung des maximalen Sollstroms.
- Neue Funktionen sind unter anderem Shelly-Ansteuerung/Rückmeldung, GPIO-Zuordnung, Wärmepumpenanzeige und die grafische/kompakte Oberfläche.
- Die Desktop-Seitenleiste mit Logo und Hinweis auf Shell Recharge Advanced 3.0 ist wiederhergestellt. Auf kleinen Bildschirmen bleibt die obere Navigation erhalten.

## Gewollte Grenzen und verbleibende Risiken

- Fünf schnelle RST-/Einschaltvorgänge innerhalb der vorgesehenen Zeitfolge lösen weiterhin einen Werksreset aus und löschen Einstellungen sowie Verlauf. Diese Funktion existierte bereits im Vergleichsstand. Ohne entsprechenden Nachweis ist sie keine bewiesene Ursache eines konkreten Datenverlusts.
- Die kleine Einstellungssicherung ist kein vollständiger Systemabzug. Eigene Experten-Regelwerte, Verlauf und getrennt gespeicherte Systemzustände benötigen die komplette Systemsicherung. Die Freigabe einer Ansteuerung ohne Rückmeldung bleibt eine gesonderte bewusste Handlung.
- Direkte ADS/ADC-Messung, manuelle RS485-DE/RE-Steuerung und der zweite allgemeine Relaisausgang waren bereits im Vergleichsstand stillgelegt. Das sind keine neu entdeckten Entfernungen in 1.23.
- Eine Firmware kann einen abgebrochenen Download im zweiten OTA-Slot abfangen. Automatisches Boot-Rollback benötigt einen passenden Bootloader. Normales OTA ersetzt ihn nicht.
- Unter derselben Versionsnummer aktualisierte Dateien werden Geräten mit bereits installierter 1.23 nicht als höhere Version angeboten. Dafür ist „Update aus Datei“ nötig.

## Prüfnachweise

- 48 Kombinationen gespeicherter Relaispolarität und Einzelkontakt-Zuordnung über die Einstellungsformate 23 bis 34. Zugangsdaten bleiben erhalten, Ladung bleibt gestoppt.
- Produktionscode des GPIO-Treibers mit simulierten HIGH-/LOW-Relaisboards: Startpegel, Pull-Widerstände, Ein-/Dreiphasenstellung, Treiberfehler und Ausschluss der GPIO-Nutzung bei Shelly/MQTT.
- Produktionscode der NVS-Lade-/Speicherfunktionen: alte und neue Schlüssel, Migration, Zugangsdaten, erhaltene ältere Datensätze und Schreibfehler.
- Produktionscode der OTA-Prüfung und des HTTP-Handlers: alte/ungültige Messwerte, Strom ab 1 A, Wartung und Schütztest blockieren Flash-Zugriffe.
- Vollständige bestehende C-/JavaScript-Regression einschließlich Energieprofilen, MQTT-Empfang, Phasensteuerung, Speicherfehlern und Oberfläche erfolgreich.
- Desktop bei 1600 CSS-Pixeln und Mobilansicht bei etwa 402 CSS-Pixeln visuell und auf horizontale Überbreite geprüft.

## Abschluss auf realer Hardware

Am 25.10.2026 wurde der GPIO-ESP per OTA auf Build `c51ed7670726` aktualisiert. Konfiguration, Sitzungen und historische Kosten blieben dabei erhalten. Nach Wiederherstellung von LOW-aktiv und der ursprünglichen Kontaktzuordnung bestätigten GPIO13 und der Anwender die tatsächliche Dreiphasenstellung. Das Relais ist dabei logisch ausgeschaltet, die Ladung bleibt gestoppt. Sieben Statusabfragen über 19 Sekunden bestätigten stabile Stellung, frische Messwerte, gespeicherte Einstellungen und erreichbare Datenquellen. Passwörter und die übrigen aktuellen Einstellungen blieben erhalten.

Der zweite gebaute Hardwarestand ist Andreas Testboard `0f18e9dbe4f0`; dessen physischer Aufbau wurde hier nicht getestet. Es erfolgte weder ein aktiver Ladetest noch ein Test am realen Wärmepumpenzähler.
