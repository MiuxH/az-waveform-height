# CDJ Player View – Community-Test 0.1.4

Erster eigenstaendiger Community-Port unserer bisherigen Player View V22.
Nur Emulator-Test, noch nicht fuer Auftritte oder den echten AZ freigegeben.
Firmware 2.00, AZ-MODS 1.2.5, exakter Tag ep147-9e9454e357389ece.
ID miuxh-player-view; der offizielle Core wird nicht ersetzt.

## 0.1.4: Schmale Player und ungeladene Decks

Die neuen Emulatorbilder bestaetigen die native Gesamtwelle samt Cues und die
Zweier-/Viereransicht. Sie zeigen abgeschnittene Tabs/PLAYER-Labels, eine
AZ-Restzeile vor dem Titel und Vergleichswellen auf ungeladenen Decks.

- Schmale Player erhalten einen eigenen Statusaufbau mit festen Spalten fuer
  PLAYER, TRACK, Zeit, TEMPO und BPM. Keine verkleinerte Kopie der breiten Anzeige.
- Uhr und BPM werden mitsamt Nachkommastellen als Gruppe eingepasst. Die kleinere
  Schrift richtet sich nach der tatsaechlich passenden Hauptschrift.
- BEAT LOOP / KEY SHIFT / BEAT JUMP werden mit nativen Fontmetriken eingepasst.
  Die grauen Tabs sind weiterhin noch keine angebundenen Audiofunktionen.
- Die native Flaeche wird ab y=70 statt y=96 bereinigt. Die offiziellen
  Performance-Overlays besitzen ein eigenes Fenster und bleiben darueber.
- Nur sicher geladene Decks werden zusaetzlich als Welle/Preview gezeichnet;
  Vergleich nur bei geladenem eigenem und geladenem Referenzdeck. Bei fehlenden
  Daten wird weder ein Playhead noch eine Vergleichswelle erfunden.
- Der offizielle native Paint-Pfad bleibt erhalten. Die Optimierung spart
  zusaetzliche Zeichnungen ungeladener Decks und nutzlose Vergleichskopien.
  Im Fixture mit einem geladenen Track sinken die zusaetzlichen nativen
  Wave-/Preview-Aufrufe der Viereransicht von 20 auf 4 (jeweils ohne Beat FX).
  Das ist eine Aufrufzaehlung, keine gemessene Hardware-FPS-Steigerung.
- 1/2 behalten ihre gemeinsamen Hoehen; zwei Player stehen nebeneinander.

Visueller Emulator-Test und FPS-Messung dieses neuen Builds stehen noch aus.
Native Tempo-/Master-/Track-Metadaten fehlen im aktuellen SDK-Port weiterhin;
sie werden nicht durch erfundene Werte ersetzt. Beat FX bleibt gemeinsam/nativ.

## 0.1.3: Darstellung nach erstem Emulator-Screenshot korrigiert

0.1.2 laedt im Emulator, zeigt jedoch native AZ-Reste rechts, eine fehlende
Gesamtwellenform und abgeschnittene Nachkommastellen. Dieser Stand behebt die
nachvollziehbaren Ursachen im Adapter:

- Gesamte Player-Flaeche inklusive rechter Restspalte loeschen; danach nur den
  echten gemeinsamen Beat-FX-Container mit nativen Kindern erneut zeichnen.
- Graphics-Zustand um den originalen Paint sichern/wiederherstellen, damit ein
  verbliebener Clip/Transform die neue Darstellung nicht beschneidet.
- Bevorzugt den vollstaendigen PlayerInfoWaveformWidget-Baum zeichnen: native
  Gesamtwelle und die dort vorhandenen Hot-/Memory-Cue-/Positions-/Zeit-Komponenten.
  Der innere WaveformWidget ist nur ein reduzierter Fallback ohne Cue-Garantie.
- Tiefensuche mit begrenztem Stack, ausgelassene Wavebox-Unterbaeume und groesseres
  Besuchsbudget verhindern, dass zahlreiche Detailmarker die Preview-Suche verdraengen.
- Zahlenbreite aus den echten SDK-Fontmetriken statt alten Bitmap-Zellen bestimmen;
  Bruchteile stehen an derselben unteren Grundlinie, unbekannte Original-BPM sind --.
- Ausgeblendete AZ-Bedienflaechen rechts unterhalb Beat FX bleiben auch fuer
  Maus-/Touch-Ereignisse gesperrt. Sichtbares Beat FX bleibt nativ bedienbar.
- Diagnose protokolliert Spielerzahl, Breite, gezeichnete Detail-/Preview-Masken
  und die native Beat-FX-Hoehe. Masken beziehen sich auf gezeichnete Player.

Diese Korrekturen sind noch kein visuell bestaetigtes CDJ-1:1-Ergebnis.
Erneuter Emulatorvergleich fuer 1/2/4 und alle drei Farben erforderlich.
Zwei Player bleiben nebeneinander; gleiche Hoehenproportionen wie ein Player.

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
- Die Gesamtwellenform stammt bevorzugt aus dem nativen PlayerInfoWaveformWidget
  samt dessen vorhandenen Hot-/Memory-Cue-Komponenten. Kein eigener PWV-Farbfilter.
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
   Unter Native renderer status steht F fuer vier gezeichnete native Details
   beziehungsweise Previews in der Viereransicht. Bei leerem Bild diese Werte mitmelden.

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
