# TeeNet-Gerätestatistik

Die zwei Dienste laufen im kostenlosen Cloudflare-Workers-Tarif und teilen sich eine D1-Datenbank.

- Meldeendpunkt: `https://teenet-heartbeat.stetastic.workers.dev/v1/heartbeat`
- Private Übersicht: `https://teenet-statistik.stetastic.workers.dev/`
- Demo: `https://stetastic.github.io/TeeNet/`

Die Firmware meldet erst nach Bestätigung der vorausgewählten Teilnahme. Übertragen werden eine zufällige Installationskennung, Softwareversion, Build und Hardwareprofil. Weder MAC-Adresse noch Messwerte, WLAN-Daten, Benutzerkonten oder Standortdaten werden übertragen. Cloudflare verarbeitet die technisch notwendige Quell-IP; die Anwendung speichert sie nicht. Server-Request-Logging ist deaktiviert. Inaktive Geräte werden nach 30 Tagen gelöscht. Eine Abmeldung stoppt Lebenszeichen und löscht den serverseitigen Datensatz beim nächsten erfolgreichen Kontakt.

Ein Lebenszeichen kommt etwa alle fünf Minuten; „online“ bedeutet maximal zehn Minuten seit dem letzten Kontakt. Die Kennung bleibt bei normalen Updates erhalten. Ein Werksreset erzeugt eine neue Kennung; ein auf andere Hardware kopierter Vollflash kann dieselbe Kennung enthalten. Die Zählung ist eine Statistik meldender Installationen, kein Nachweis physischer Hardware und kein Zähler aller historischen Installationen.

Jede Installation erzeugt einen eigenen zufälligen Schlüssel. Nur dessen Hash wird gespeichert. Fremde Schlüssel können bestehende Geräte nicht aktualisieren oder abmelden. Öffentlich erreichbare Registrierung ist gegen einfache Massenregistrierung gebremst, kann aber nicht die Echtheit beliebiger Geräte beweisen.

## Zugang

Ein persönliches Kennwort schützt die Statistik. Es wird mit PBKDF2-SHA256 und einem zufälligen Salt gespeichert. Sitzungen verwenden serverseitig gehashte, zufällige Tokens in sicheren HttpOnly-/SameSite-Cookies und laufen nach zwölf Stunden ab. Wiederholte Fehlanmeldungen werden begrenzt. Die Zahlen sind ohne gültige Sitzung nicht zugänglich. Der einmalige Einrichtungsschlüssel wurde nach der Ersteinrichtung entfernt.

Das Kennwort nicht in Firmware, Quellcode, öffentlichen URLs oder GitHub speichern. Kontowiederherstellung erfolgt durch den Cloudflare-Kontoinhaber: Zugangsdaten und Sitzungen gezielt aus den Tabellen `admin_credentials` und `admin_sessions` entfernen und mit `scripts/provision-setup.mjs` einen neuen einmaligen Link erzeugen. Die Geräte-Datensätze müssen dabei nicht gelöscht werden.

## Entwicklung

1. Offizielles Wrangler installieren und mit dem vorgesehenen Cloudflare-Konto anmelden.
2. `schema.sql` bei einer Neuinstallation einmal auf die konfigurierte D1-Datenbank anwenden.
3. Tests: `node --test tests/worker.test.mjs`.
4. Veröffentlichung: `wrangler deploy --config wrangler.ingest.json` und `wrangler deploy --config wrangler.admin.json`.
5. Live-Test: `python scripts/verify-live.py` erzeugt einen Testdatensatz und entfernt ihn anschließend wieder.

Bei Schemaänderungen bestehende Daten erhalten und eine neue begrenzte Migration anlegen. Keine bezahlten Tarife automatisch aktivieren. Das Workers-/D1-Free-Kontingent wird mit anderen Diensten desselben Kontos geteilt; bei Erreichen der Limits können Meldungen ausfallen. Die lokale Laderegelung bleibt unabhängig davon funktionsfähig.
