# TeeNet für ioBroker · Version 0.2.0

Wallbox steuern, Messwerte anzeigen und vorhandene PV-/Akku-Datenpunkte verbinden.
Kompatibel mit der MQTT-Schnittstelle von Version 1.13. Die ESP-Firmware muss für den Adapter nicht geändert werden.

## Einrichten

1. Das Paket auf dem ioBroker-Rechner installieren: `iobroker url /pfad/iobroker.teenet-0.2.0.tgz`. Danach eine Instanz anlegen: `iobroker add teenet`.
2. In den Instanz-Einstellungen die Adresse und den Port des vorhandenen MQTT-Brokers eintragen. Benutzername und Passwort bei Bedarf ergänzen.
3. Das Topic-Präfix aus TeeNet übernehmen, normalerweise `wallbox-ems`. Für jedes weitere Gerät eine eigene Instanz und ein eigenes Präfix verwenden.
4. Speichern und die Instanz einschalten. `teenet.0.info.connection` wird erst bei aktuellen ESP-Rückmeldungen wahr.
5. Zum Bedienen „Nur lesen“ ausschalten. Ein Start des Adapters selbst startet keine Ladung.

Voraussetzung: Node.js ab 22.19, ioBroker js-controller ab 6 und Admin ab 7. Ein vorhandener MQTT-Broker reicht; der allgemeine MQTT-Adapter ist nicht zusätzlich erforderlich. Jede laufende Instanz erhält eine eigene MQTT-Client-ID.

## Bedienen

| Datenpunkt unter `teenet.0` | Funktion |
| --- | --- |
| `control.enabled` | `true`: Ladefreigabe, `false`: Stoppen |
| `control.mode` | `manual`, `pv` oder `off`. Manuell/PV erteilt auch die Ladefreigabe. |
| `control.power_kw` | Leistung in kW; startet den Manuellbetrieb. `0`: Aus. |
| `control.battery_use` | Akku-Wolkenpuffer |
| `control.battery_start_use` | Auto aus Hausakku laden |
| `wallbox.power_w` | Gemessene Ladeleistung in W |
| `wallbox.block_reason` / `wallbox.locked` | Status und Ladesperre |
| `house.power_w` | Netzbezug positiv, Einspeisung negativ |
| `energy.*` | Gesamtenergie, Tagesenergie und vom ESP gesendete Tageskosten |
| `info.commandStatus` / `info.lastError` | Befehlsrückmeldung und Fehler |

Werte mit `ack=false` schreiben. Der Adapter bestätigt den Wert erst, wenn neue ESP-Rückmeldungen dazu passen. Die Rückmeldung bestätigt die Anforderung, nicht den tatsächlichen Beginn einer Ladung. Dieser lässt sich an der Ladeleistung erkennen. Ohne passende Rückmeldung endet der Versuch nach 45 Sekunden; der Befehl wird nicht wiederholt.

Ein Stopp ersetzt sofort einen ausstehenden Befehl. Einschalten nach dem Modus „Aus“ stellt den zuletzt beobachteten Manuell-/PV-Modus wieder her; nach einem Adapter-Neustart wird zunächst Manuell verwendet. Andere neue Befehle benötigen aktuelle Steuerungsrückmeldungen. Ein noch nicht abgeschlossener Befehl wird nicht durch eine zweite Leistungsvorgabe überschrieben.

Zulässige Leistungsstufen: `0`, einphasig 2–3,5 kW oder dreiphasig 6–11 kW, jeweils in 0,5-kW-Schritten. Der eingerichtete Anschluss und die Ladegrenzen im ESP bestimmen, welche Stufen tatsächlich möglich sind. Fehlende Telemetrie sperrt neue Adapter-Befehle. Eine Ladesperre in der TeeNet-Oberfläche prüfen und dort bei Bedarf aufheben.

## PV und Akkus verbinden

Unter **PV & Akku** die gewünschte Weitergabe aktivieren und die originalen Zahlen-Datenpunkte auswählen. Keine TeeNet-Ausgänge als Quelle wählen.

- PV: tatsächliche Solarleistung, keine Wechselrichterleistung einschließlich Akkustrom.
- Hausakku: SOC in %. Negative SOC-Werte werden als 0 % weitergegeben.
- Akkuleistung: zwei positive Werte für Laden/Entladen oder ein Wert mit Vorzeichen. Die Vorzeichenrichtung lässt sich auswählen.
- Fahrzeug: SOC in %. Der Adapter fragt keine Fahrzeug-Cloud ab.

W/kW werden anhand der Einheit des ioBroker-Datenpunkts umgerechnet. Ohne Einheit wird W angenommen. SOC ohne Einheit wird als Prozentwert gelesen. Beide getrennten Akkuleistungsquellen müssen ausgewählt und aktuell sein.

In TeeNet unter **Smart Home & MQTT** die Verbindung einrichten und unter **Funktionen** die benötigten Hausakku-/PV-/Fahrzeuganzeigen aktivieren. Frühere Weitergabe-Skripte für dieselben Werte ausschalten, damit nur eine Quelle sendet. Eine direkte Huawei- oder OpenDTU-Quelle im ESP hat Vorrang; sie bei dieser Anbindung nicht gleichzeitig für dieselben Werte nutzen.

Das Quellalter zählt ab der letzten Aktualisierung des Datenpunkts (`ts`, nicht der letzten Wertänderung `lc`). Ungültige, nicht bestätigte (`ack=false`) oder veraltete Quellen werden nicht als frische Messung wiederholt. Stattdessen erhält der ESP `false` am passenden Gültigkeitsthema. Deaktivierte Quellen werden weder abonniert noch weitergegeben.

**Der Adapter steuert keinen Wechselrichter.** Er kann das Entladen des Hausakkus nicht sperren. Tibber-Preissteuerung ist in dieser Testversion nicht enthalten.

## Verbindung und Verbrauch

ESP-Messwerte werden über die vorhandenen MQTT-Themen empfangen. Die aktuelle Firmware sendet normale Messwerte etwa alle 20 Sekunden und Energie etwa jede Minute. Die Weitergabe ausgewählter Eingangswerte erfolgt alle 5 oder 10 Sekunden. Nur das optionale Ladearchiv verwendet HTTP.

`info.connection` wird nur bei aktiver Brokerverbindung und frischen ESP-Messwerten wahr. Gespeicherte MQTT-Messwerte allein gelten nicht als Verbindung. Veraltete Werte bleiben mit Fehlerqualität sichtbar und dürfen nicht als aktuelle Messung genutzt werden.

`energy.today_cost` ist der vorhandene MQTT-Tageskostenwert des ESP. Getrennte Netz-/Solarkosten und einzelne Ladesitzungen stellt die jetzige schlanke MQTT-Schnittstelle nicht bereit; sie bleiben in der TeeNet-Oberfläche. Hausakku-/PV-Rückmeldungen unter `battery.*` und `pv.*` sendet der ESP derzeit nur bei aktivierter Home-Assistant-Ausgabe. Weitergegebene Quellen bleiben an ihrem ursprünglichen ioBroker-Datenpunkt sichtbar; der Adapter legt dafür keine Kopien an.

Der Adapter besitzt 22 Datenpunkte. Technische Rückmeldungen zur Befehlsbestätigung und zur Prüfung ausgewählter Quellen verarbeitet er intern. Frühere doppelte `inputs.*`-, Ampere-, Modus- und Freigabeobjekte werden beim Update entfernt.

## Entwickeln und testen

```sh
npm install
npm test
npm run check
npm run test:controller
```

Die Tests prüfen Befehle, Rückmeldungen, Ausfälle, Quellenalter und einen echten lokalen MQTT-Broker. Die Adapter-Steuerung wurde zusätzlich mit der realen TeeNet-Installation geprüft.

## Stand

0.1.3: Rückmeldung der unteren TeeNet-Ladegrenze korrigiert. Der Objektbaum bleibt auf 22 wesentliche Datenpunkte begrenzt. Noch keine Aufnahme in das öffentliche ioBroker-Adapterverzeichnis und keine npm-Veröffentlichung. Die Adapter-Version ist unabhängig von der ESP-Version.

## Ladearchiv ab TeeNet 1.25

Unter **Erweitert → Ladearchiv** die Adresse des ESP eintragen. Die aktuelle Firmware meldet gespeicherte, abgeschlossene Sitzungen per MQTT unter `sensor/session_archive` im eingestellten Topic-Präfix. Der Adapter übernimmt per HTTP nur die Sitzungen seit dem letzten gesicherten Eintrag. Er gleicht bei einem Neustart, nach einer MQTT-Unterbrechung und stündlich zur Absicherung ab. Bei älterer Firmware ohne Teilabruf bleibt das Fünf-Minuten-Intervall bestehen. Die Archivierung funktioniert auch bei schreibgeschützter Wallboxsteuerung. Ein leeres Adressfeld beendet den Abruf. Für denselben ESP den separaten Archivskript-Timer ausschalten, sobald der Adapter übernimmt.

Die Monatsarchive liegen dauerhaft unter `0_userdata.0.TeeNet.Archiv.<Gerät>.<JJJJMM>` und lassen sich mit der ioBroker-Datenbanksicherung sichern. Bestehende Monatsarchive des TeeNet-Skripts werden weiterverwendet. Gerätekennung, Startzeit und Sitzungsnummer verhindern Doppelzählungen. Nach einem Ausfall holt der Adapter alle abgeschlossenen Sitzungen nach, die der ESP noch gespeichert hat.

Unter `teenet.0.stats` stehen Energie, Anzahl der Ladesitzungen, Solaranteil und die getrennten Kosten für heute, den laufenden Monat, das laufende Jahr und seit Aufzeichnungsbeginn. `months` enthält die Monatswerte als JSON, `recentSessions` die letzten zwölf Sitzungen. `lastSync` zeigt den letzten erfolgreichen Abruf. Für die Zuordnung zählt das Startdatum der Sitzung in der Zeitzone von TeeNet. Sitzungen ohne Datum fließen in die Gesamtsumme ein. Solar umfasst den Hausakku. TeeNet rechnet Energie unbekannter Herkunft dem Netz zu. Die Kosten stammen aus der jeweiligen Sitzung.
