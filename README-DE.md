# MiuxH – 4-Deck Waveform Height 0.1.2 Beta

Eigenstaendige Community Mod fuer die native Vierdeck-Ansicht des XDJ-AZ.
Enable schaltet sie ein; elf Hoehenstufen reichen von 0 Prozent (Original)
bis 100 Prozent (fast aneinander). Native Farben und Beatgrid-Hoehen bleiben erhalten.

## Installation direkt am Deck

1. Firmware **2.00** und offizielle **AZ-MODS 1.2.5** verwenden.
2. Community Mods > Repositories > Add: **MiuxH/az-waveform-height**.
3. Browse > 4-Deck Waveform Height > Install > Reload now.
4. Einstellungsseite oeffnen, Enable einschalten und Hoehe waehlen.

Bei manueller USB-Kopie fragt das Deck unter Installed einmal nach Allow.
Die offizielle Einstellung `four-waveforms` fuer unseren Hoehentest ausschalten:
unsere Mod gibt ihr sonst Vorrang. Community-Mod deaktivieren und Reload now,
um sie vollstaendig zu entladen. Der offizielle Core wird nicht ersetzt.

## Bestaetigter Teststand, 23.10.2026

Der Autor bestaetigte im Emulator Bedienung, Hoehenstufen, Grid/Marker und
Wechsel zwischen 2 und 4 Decks. Anschliessend bestaetigte er die Funktion auf
dem echten XDJ-AZ mit Firmware 2.00 und AZ-MODS 1.2.5.
Der Hardware-Test erfolgte mit dem manuell kopierten 0.1.2-Testpaket.
Die Installation ueber das veroeffentlichte Repository ist noch separat zu pruefen.
Keine instrumentierte FPS-Messung, kein Langzeittest und kein detaillierter
Hardware-Testbericht pro Farbmodus liegen vor. Deshalb bleibt der Stand Beta.

53 native Fixturechecks bestanden in plain, ASan/UBSan und TSan.
Leak-Pruefung war wegen Prozesssichtbeschraenkungen abgeschaltet.
Der ARM64-Build und offizielle Paketvalidator bestanden.
Das generische SDK-Harness fuer services-only Mods deckt diesen nativen Hook
nicht ab; dieses Projekt nutzt eigene native Objekt- und SDK-Fixtures.

## Technische Grenzen und Verhalten

Nur Firmware-Tag `ep147-9e9454e357389ece` ist zugelassen.
Firmware-spezifischer Code-Hook am nativen Component-Paint-Dispatcher, auf
WaveformView begrenzt. Keine Umgehung der SDK-Bereichs- und Besitzpruefungen.
Die offiziellen Waveform-/FPS-Hooks bleiben in der Aufrufkette.
Unbekannte Layouts fuehren zu keinen Geometrie-Schreibzugriffen.
Kein Netzwerkzugriff, keine direkten Dateizugriffe, keine Audioaenderungen.
Enable und Hoehe werden ueber den Konfigurationsdienst des offiziellen Core gespeichert.
Die Seite unterstuetzt Tippen und Drehknopf, keinen kontinuierlichen Touch-Drag.

## Selbst bauen und veroeffentlichen

Community-Mod-bauen.bat verwendet den festgelegten offiziellen SDK-Commit
3dcaf8f7331ea94135d7161f653edfaf906d5ea6.
Tests: `python3 tests/run.py --passes plain asan tsan`.
Ein Release-Tag `miuxh-wave-height-0.1.2` startet den Release-Workflow.
Der Workflow baut und validiert das Paket, veroeffentlicht es als Prerelease
und aktualisiert azmods-repo.json mit echter Groesse und SHA256.
Die erste Beta kann auch ueber release-request.json auf main angefordert werden.

Die Aufnahme in Kyle-Hosman/az-mods-index ist ein separater Review-Prozess.
Ohne Aufnahme koennen Benutzer das Repository bereits selbst hinzufuegen.
GPL-3.0-only, siehe LICENSE und NOTICE. Keine Firmware, Musik oder Schluessel enthalten.
