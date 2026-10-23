# MiuxH – 4-Deck Waveform Height

Eigenstaendige Community-Mod fuer den offiziellen AZ-MODS-Core ab 1.2.3.
Sie erweitert die Hoehe der nativen Waveforms in der normalen 4-DECK-Ansicht,
von Original bis fast aneinander. Die nativen Beatgrids behalten ihre Hoehe.
Blue, RGB und 3 Band benutzen weiterhin die originalen AZ-Zeichner.

**Status: experimenteller SDK-Port 0.1.1.** Der ARM64-Build und der offizielle
Paketvalidator sind bestanden. [Testpaket herunterladen](https://github.com/MiuxH/az-waveform-height/actions/runs/40021689643/artifacts/12529959088).
0.1.0 scheiterte im Emulator beim Initialisieren; der korrigierte 0.1.1-Port
muss noch im Emulator und auf dem echten Deck getestet werden. Es gibt kein
Katalog-Release. Den alten Testpaket-Link fuer 0.1.0 nicht mehr verwenden.

## Fuer deinen ersten Test

1. Den nach deinem Update vorhandenen **offiziellen Core beibehalten**.
   Weder azmod.so noch azmod-core.so werden durch dieses Projekt ersetzt.
   Die Entwicklerfreigabe fuer einen unsignierten Core ist nicht erforderlich.
2. ZIP in einen eigenen Ordner az-waveform-height entpacken. Docker Desktop
   starten und **Community-Mod-bauen.bat** doppelklicken. Python und Git muessen
   vorhanden sein. Git unter C:\Program Files\Git wird ebenfalls gefunden.
3. Das Werkzeug holt den genau festgelegten offiziellen SDK-Commit, baut in
   dessen ARM64-Container, fuehrt den offiziellen Paketvalidator aus und erzeugt
   **dist/miuxh-wave-height-0.1.1.azmod**. Ein falscher Host-/x86-Build wird vom
   Validator abgelehnt. Es werden keine Firmware-Assets benoetigt.
4. Diese .azmod-Datei ist ein ZIP. Mit 7-Zip in einen Ordner entpacken oder eine
   Kopie in .zip umbenennen. Die vier Dateien azmod.json, miuxh-wave-height.so,
   icon.png und LICENSE gehoeren direkt unter:

   `USB:/MODS/community-mods/miuxh-wave-height/`

5. In der vorhandenen USB-Datei MODS/azmods.cfg die folgende eigene Zeile
   hinzufuegen bzw. ihren bestehenden Wert auf on setzen; andere Zeilen behalten:

   ```ini
   community-mods.miuxh-wave-height = on
   ```

6. Stick sicher auswerfen und AZ damit neu starten. Unter
   **SOURCE > MOD MENU > Community Mods > Installed > 4-Deck Waveform Height**
   den neuen Build mit **Allow** bestaetigen, danach **Reload now**.
   Das Deck genehmigt genau den Hash dieser eigenen Mod-Datei; der Core bleibt
   offiziell. Reload now schliesst die laufenden Tracks, also vorher stoppen.
7. Die eigene Einstellungsseite oeffnen: **Enable** einschalten und **Height**
   einstellen. Der Regler ist durch Antippen auf 0, 10, ... 100 Prozent oder
   mit dem Browse-Drehknopf bedienbar. Knopfdruck schaltet Enable um; BACK geht
   zurueck. Die Werte werden ueber SDK cfg_get/cfg_set gespeichert.

Die aktuelle SDK-Menue-Seite liefert TAP und Drehknopf-Ereignisse, aber keinen
kontinuierlichen MOVE-Strom wie die alte Core-Menueimplementierung. Deshalb wird
hier kein durchgehendes Finger-Ziehen behauptet. Die Reglerwerte selbst bleiben
die gleichen elf Stufen wie in V4. Die eigene Hauptmenue-Zeile hat ein Waveform-Icon.

Nur dieser EP147-Build wird zugelassen:

- bekannt als Firmware 2.00, MD5 8daa1de55d7328f458f6d855cdc3cdde;
- SDK-Tag **ep147-9e9454e357389ece**;
- ein anderer Tag wird abgelehnt; niemals "any" fuer diese native Layoutmod.

Auf dem Deck steht der Tag unten unter Community Mods > Repositories. Ein
offizielles AZ-MODS-Core-Update ersetzt diese separate Community-Datei nicht.
Andere Firmware kann jedoch einen neuen Port benoetigen; ein Core-Update kann
einen Hook-Konflikt verursachen, der dann sauber abgelehnt werden muss.

## Veroeffentlichung fuer andere DJs

Oeffentliches Quellcode-Repository: **MiuxH/az-waveform-height**. azmods-repo.json ist zunaechst absichtlich
leer: Es gibt noch keinen echten Download, dessen Groesse und SHA256 eingetragen
werden koennten. Es enthaelt keine Platzhalter-Pakete oder erfundenen Pruefsummen.

Der einfachste Weg aus diesem Chat ist die GitHub-Anbindung an dein Konto MiuxH.
Alternativ kannst du **GitHub-Projekt-anlegen.bat** lokal starten. Das Skript
verwendet GitHub CLI, prueft das angemeldete Konto, legt nur ein neues oeffentliches
MiuxH/az-waveform-height an und ueberschreibt kein bestehendes Repository.
Falls GitHub CLI fehlt: `winget install --id GitHub.cli -e`, dann Terminal neu
oeffnen. Die Anmeldung erfolgt mit `gh auth login` im Browser. Kein Token oder
Schluessel gehoert in diese Quelldateien.

Der erste Push startet unter **GitHub > Actions > build-test-package** automatisch
Tests und den ARM64-Build. Das Artefakt **miuxh-wave-height-test** enthaelt das
Testpaket. Es ist noch kein Release und wird noch nicht vom Deck-Katalog angeboten.

Nach dem echten Laufzeittest und einem Commit seiner Ergebnisse:

```powershell
powershell -ExecutionPolicy Bypass -File .\Publish.ps1 -Release
```

Der Tag miuxh-wave-height-0.1.1 startet den offiziellen SDK-Release-Ablauf:
Build, Validator, .azmod-Paket und Icon als GitHub-Release, danach ein Commit mit
dem echten Paket-URL, SHA256 und Groesse in azmods-repo.json. Der erste Stand wird
als experimentelles Prerelease markiert. Actions muss den Repository-Inhalt
schreiben duerfen; ein Branch-Schutz kann den letzten Index-Commit blockieren.
Das Ergebnis unter Actions pruefen, bevor du den Repository-Namen weitergibst.

Danach koennen andere am Deck unter **Community Mods > Repositories > Add**
`MiuxH/az-waveform-height` hinzufuegen und die Mod unter Browse installieren.
Damit sie ohne Hinzufuegen standardmaessig erscheint, muss dieses Repository
zusaetzlich per Pull Request in **Kyle-Hosman/az-mods-index** aufgenommen werden.
Das erfordert die Pruefung und Freigabe der Projektbetreiber; sie wird hier nicht
vorweggenommen. Quellcode, Lizenz und echte Testresultate gehoeren dazu.

## Zurueck und Kompatibilitaet

- Eigenes Enable aus oder Height 0 stellt die native Geometrie wieder her,
  sobald die normale Waveform-Ansicht zeichnet.
- Community-Mod abschalten und die Aenderung mit Reload now anwenden startet
  EP147 ohne ihren Hook. Es muss kein gesicherter Core zurueckkopiert werden.
- Das Projekt schreibt weder Musik/Library noch Audio- oder Beat-FX-Parameter.
  Der physische Beat-FX-Bereich wird nicht verschoben oder neu gezeichnet.
- Falls ein Core bereits die alte integrierte Hoehenmod aktiv hat, bekommt diese
  Vorrang; diese Community-Mod macht dann keine konkurrierenden Hoehenaenderungen.
  Der vorgesehene Testpartner ist der unveraenderte offizielle Core.
- Ein bereits belegter nativer Zeichnungsdispatcher wird ueber das SDK
  abgelehnt, nicht selbst ueberschrieben. Eine generelle Kompatibilitaet mit anderen Layoutmods ist damit
  nicht bestaetigt. Die spaetere CDJ-Player-View-Mod gehoert nicht zu diesem Paket.

## Technischer Stand / Checks

Native Schnittstelle ab 0.1.1: SDK claim_hook an
Component::paintComponentAndChildren (Firmware 2.00: 0x25d4d30), gefiltert auf
die genaue WaveformView-Vtable. Die Funktion ruft den nativen paint-Slot,
danach die Kinder und paintOverChildren auf. Jeder Originalaufruf bleibt
bestehen; die offiziellen WaveformView-/FPS-Hooks werden nicht ersetzt.
0.1.0 scheiterte mit init=5: Die SDK-Datenbereichspruefung lehnt die Vtable in
EP147s ausfuehrbarem .rodata-Segment ab. Eine Verschiebung der Hauptprogramm-
Adresse war nicht die Ursache. Die SDK-Pruefung bleibt unveraendert.
Zusaetzlich akzeptiert die Zeigerpruefung jetzt hohe 48-Bit-Userspace-Adressen
im Emulator. Kerneladressen und ungerade Pointer bleiben ausgeschlossen.

Native Objekte werden nur waehrend dieses EP147-Message-Thread-Callbacks aus dem
aktuellen JUCE-Kinderbaum gelesen, mit Vtables, Parent-Verbindungen, nativer
virtueller Basis und eindeutigen Decknummern geprueft. Nach einem Relayout werden
alle Kinder neu gelesen. Die Layoutmodelle speichern nur Identitaetswerte und
Zahlen; keine spaeter dereferenzierten Komponentenpointer. Unvollstaendige
Viererlayouts, unbekannte Decks und zu kleine Raster werden vor Schreibzugriffen
abgelehnt. Keine Datei-/Netzwerkzugriffe, Threads oder Allokationen im Paint-Hook.

Lokal bestanden: **28 Adapter-/SDK-/Touch-Checks jeweils plain, ASan/UBSan und
TSan**, einschliesslich gleichzeitiger Einstellungswechsel und nativer Frames.
Ein separater Host-Link mit versteckten Symbolen exportiert nur
az_mod_v1_entry. Das generische SDK-Harness verlangt services-only/"any" und
eignet sich deshalb nicht unveraendert fuer diesen nativen Hook-Port; make test
verwendet die eigenen SDK- und nativen Objektfixtures. LeakSanitizer ist hier
wegen Prozesssichtbeschraenkungen ausgeschaltet.

ARM64-Build, offizieller Gesamt-Paketvalidator und Paketerstellung in GitHub
Actions bestanden. Paket 0.1.1: 63 594 Bytes, SHA256
`fb4cb8da221c6d4c3d19ff40b3ebaa59d13b69cab21209adf90bd4a6a0e36c89`.
Noch offen: Emulatorlauf und echter AZ-Test. Die Hosttests verwenden simulierte Objekte und
beweisen keine native Laufzeitstabilitaet. Der oeffentliche Mod Index sollte erst
nach diesen Nachweisen beantragt werden. Die CI-Tests ersetzen keinen Audio-Test.

Abnahme auf einem Test-Deck: alle elf Stufen, Ein/Aus und 0 Prozent; Blue/RGB/3Band;
alle vier Decks inkl. leere Decks; Track laden/wechseln bei 100 Prozent;
2/4-DECK-Wechsel; SOURCE/INFO zurueck; langer Wiedergabelauf; Beat FX und Stems;
Neustart mit gespeicherten Werten; Deaktivieren der Community-Mod. Ergebnisse
mit Firmware-Tag und Core-Version dokumentieren.

GPL-3.0-only, siehe LICENSE und NOTICE. Keine Firmware, Assets, Musik, privaten
Captures oder Zugangsdaten enthalten. SDK-Commit:
3dcaf8f7331ea94135d7161f653edfaf906d5ea6.

## Teststand 0.1.1

0.1.0 wurde im Emulator gefunden und freigegeben, scheiterte aber beim Hook
mit "not inside EP147's data". 0.1.1 behebt diesen Adapterfehler; sein echter
Emulatorlauf und der Hardware-Test bleiben offen. Die 28 lokalen Fixturechecks
decken auch hohe Emulatorzeiger und unveraenderte andere Komponenten ab.
Die aktuellen Paket-Links und Pruefsummen beziehen sich auf 0.1.1.
