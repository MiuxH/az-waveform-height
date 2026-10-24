# CDJ Player View – Community-Test 0.1.2

Erster eigenstaendiger Community-Port unserer bisherigen Player View V22.
Nur Emulator-Test, noch nicht fuer Auftritte oder den echten AZ freigegeben.
Firmware 2.00, AZ-MODS 1.2.5, exakter Tag ep147-9e9454e357389ece.
ID miuxh-player-view; der offizielle Core wird nicht ersetzt.

## 0.1.2: Hook-Einstieg korrigiert

Die Diagnose des 0.1.1-Laufs bestaetigt den Paint-Hook, aber der zweite Hook
am getComponentAt-Einstieg wird vom SDK-Installer abgelehnt. Der erste
Vier-Instruktionen-Bereich enthaelt einen TBZ-Sprung. Der Hook sitzt jetzt
acht Bytes spaeter an der ersten Stack-Frame-Instruktion; die native
Sichtbarkeitspruefung bleibt davor erhalten. Argumente und Stack sind dort
unveraendert, der zuvor verwendete Scratchwert ist vor jeder weiteren Nutzung
ueberschrieben. Ein 32-Byte-Fingerprint wird vor allen Hook-Claims geprueft.
Die SDK-Relokations-, Besitz- und Laufzeit-Patchpruefungen bleiben aktiv.
Neuer ARM64-Build; vor Installation Emulator vollstaendig neu starten.

## 0.1.1: Versionssperre korrigiert

0.1.0 wurde vor init mit "Needs AZ-MODS 1.2.5 or newer" abgewiesen.
Die benutzten SDK-Dienste existieren ab 1.2.3 und werden einzeln mit AZ_HAS
geprueft. Deshalb ist min_core jetzt 1.2.3. Fuer den Test weiterhin den
offiziellen AZ-MODS-Satz 1.2.5 verwenden. Die ARM64-Bibliothek bleibt
bytegleich zum validierten 0.1.0-Testbuild; nur das Paketmanifest aendert sich.

## Was in diesem Stand umgesetzt ist

- Bestehende V22-Aufteilung/Chrome: 1 Player, 2 Player nebeneinander,
  4 Player im Raster. 1 und 2 Player haben gleiche Hoehenproportionen.
- Hardware-Deckwahl wird nur beobachtet: Einplayer folgt dem Fokus,
  Zweiplayer folgt linker/rechter Hardwareseite. Kein Master- oder FX-Wechsel.
- Detailwaveform wird durch den echten nativen Waveform-Box-Zeichner gemalt.
  Native Cue-Komponenten, Playhead und Beatgrid bleiben in dessen Kindbaum.
- Die Gesamtwellenform stammt aus dem echten nativen WaveformWidget samt
  dessen vorhandenen Hot-/Memory-Cue-Komponenten. Kein eigener PWV-Farbfilter.
- BLUE, RGB und 3 BAND folgen der nativen Farbeinstellung unter SHORTCUT.
  Es werden keine Analysedaten oder nicht geladene Cues erfunden.
- Beatgrid-Leisten werden separat vom Audioinnenbereich eingepasst: die
  nativen Hoehen der beiden Ruler werden nicht zusammen mit der Welle gedehnt.
- Beschriftungen werden durch die SDK-Dienste mit den installierten AZ-Schriften
  gezeichnet. Kein eingebauter Bitmap-Fallback, keine ausgelieferten Schriftdateien.
  Die genaue Uebereinstimmung einzelner Zahlenfonts muss visuell geprueft werden.
- Echte SDK-Trackdaten fuer Titel, Zeit, BPM und Key. Unbekannte Daten bleiben --.
- Nativer Beat-FX-Container inklusive Expand-Pfeil wird als gemeinsame rechte
  Flaeche reserviert. Keine Effektinstanz, kein Parameterreset, kein Audioeingriff.
- Titel-/Statusfenster liegen nur in schmalen Leisten. Die Waveforms werden
  direkt in EP147 gezeichnet und nicht ueber den SDK-Overlay-Tick kopiert.
  Die offiziellen 60-FPS-Hooks bleiben im Pfad. 60 FPS sind damit NICHT gemessen.
- SOURCE/BROWSE bleiben native gemeinsame Ansichten; native INFO/Shortcut/Utility
  setzen die Komposition aus. Deaktivieren stellt den gespeicherten AZ-Modus her.

## Noch nicht auf volle V22-Funktion uebertragen

Dieser Stand ist kein vollstaendiger Ersatz der bisherigen Core-Integration.
Die unveraenderte bisherige Arbeit ist unter legacy-v22/ zum Weiterportieren erhalten.

Die Audio-Touch-Sheets fuer Beat Loop, Key Shift, Jump, Touch Seek/Preview und
unser manuell gesetzter Taktzaehler sind noch nicht angebunden. Die Tabs sind grau;
sie loesen keine scheinbar funktionierenden Audioaktionen aus. Native physische
Tasten bleiben unveraendert. Die Preview ist in diesem Port zunaechst Anzeige.

Die SDK-Snapshots liefern keine Sync-Master-Identitaet und nicht alle bisherigen
Utility-/Tempo-/Loop-Daten. Der Phasenvergleich benutzt deshalb ausdruecklich
nur den ausgewaehlten Gegenplayer. Er behauptet keinen Masterzustand und ist
noch nicht der endgueltige CDJ-Master-Phasemeter. Umstellung per Einstellung;
Antippen der Vergleichszone wird fuer den Touch-Panel-Pfad behandelt.

Koordinatenverschobene Audioflaechen werden im Touch-Panel-Filter abgefangen.
Zusaetzlich weist ein separater nativer Hit-Test-Hook die verschobene
Player-Flaeche fuer X11-Mausklicks zurueck. So sollen dort keine unsichtbaren
Audio-Buttons ausgeloest werden. Die endgueltige Maus-/Touch-Weiterleitung
muss noch portiert und im Emulator bestaetigt werden.

Ein nativer BLUE400-Preview-Cache bleibt in seiner urspruenglichen Datenaufloesung.
Native Darstellung garantiert keine zusaetzlichen Audiodetails beim Vergroessern.
Ob das native Preview-Widget in allen Ansichten an unserem Ort erkannt wird,
muss der Emulator-Test zeigen; fehlende native Widgets bleiben leer.

Stems-Audio, 60-FPS-Mod und Overlays werden nicht abgeschaltet oder ersetzt.
Die Positionen zusaetzlicher Stem-Overlayfenster sowie Beat-FX-/INFO-Bedienung
sind in der neuen Kombination noch nicht laufzeitgeprueft.

## Installation fuer den Emulator

1. Emulator mit Firmware 2.00 und OFFIZIELLEN AZ-MODS 1.2.5 starten.
2. Community Mods einschalten. Phase Meter, Four Waveforms/Height und
   Hide Track Title fuer diesen Test ausschalten.
3. Player-Test-installieren.bat aus dem bereitgestellten Testpaket starten.
   Ausschliesslich lokaler QEMU auf 127.0.0.1:2222; kein echter USB-Stick.
4. Community Mods > Reload now; Installed > CDJ Player View > Allow > Reload now.
5. Die Community-Mod-Zeilen CDJ Player View (Enable), Players und Comparison
   einstellen. Am Anfang keine Tracks abspielen, erst alle Ansichten ansehen.
6. Mit AZ Control / physischen Tasten Tracks laden und Play testen.
   Farbe am nativen SHORTCUT waehlen; fuer jede Farbe 1/2/4 ausprobieren.
7. Player-Test-pruefen.bat speichert Initialisierung/EP147-Diagnose.
   Unter Native renderer status steht F fuer vier gefundene native Details
   beziehungsweise Previews. Bei leerem Bild diese Werte mitmelden.

Ein konkurrierender offizieller Layout-Schalter deaktiviert unsere eigene
Enable-Zeile; fremde Mod-Zeilen duerfen wir per SDK nicht umschalten.
Die separate Waveform-Height-Community-Mod fuer diesen ersten Test deaktivieren.

Bei Problemen: Player-Test-deaktivieren.bat, dann Community Mods > Reload now.
Der offizielle Core bleibt erhalten. Keine Freigabe fuer den echten AZ in diesem Stand.

## Pruefstand

Native Objekt-/Render-Fixtures bestanden mit plain, ASan/UBSan und TSan.
Sie pruefen die Zuordnung, Graph-Ablehnung, Mode-Restore, native Renderaufrufe,
Transform-Slices, Surface-Leisten, Beruehren/Bewegen/Loslassen und nebenlaeufige
Snapshot-Publikation. LeakSanitizer ist fuer den prozesslangen Renderpuffer aus.
Es sind simulierte native Objekte, kein Beweis fuer reale Firmwaredarstellung.
ARM64-Build/SDK-Validator: siehe TEST-STATUS.json und GitHub Actions.
Emulatorlauf, Screenshots, Bildraten und Audio auf dieser neuen Version: offen.

## Technik und Lizenz

Zwei SDK-Code-Hooks: Component::paintEntireComponent (0x25d4a80) und
Component::getComponentAt(Point<int> const&) (Hookstelle 0x25f03d8 hinter dem Einstieg 0x25f03d0). Paint ist
gefiltert auf die genaue WaveformView. Kein Hook auf die bereits vom Core
belegte WaveformView-Paint-Funktion oder den Height-Mod-Dispatcher.
Native Zeiger werden nur im aktuellen GUI-Paint aus dem Live-Kindbaum geborgt;
nach dem originalen Paint wird der Baum neu gelesen. Keine Zeiger gehen an
Overlay-/Bus-Threads. Diese bekommen nur atomar publizierte numerische Daten.
Kein Netzwerk, keine direkten Dateizugriffe, keine eigene Audioverarbeitung.
Einstellungen werden durch die offiziellen SDK-Mod-Menu-Zeilen gespeichert.
GPL-3.0-only, siehe LICENSE und NOTICE. Keine Firmware, Musik, Keys oder Fonts.
