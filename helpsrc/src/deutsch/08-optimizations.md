# Geschwindigkeits-Optimierungen

Einige Funktionen von TinyDev benötigen vergleichsweise viel
Rechenleistung, einen schnellen Massenspeicher oder viel RAM.

Generell gilt, dass die Grafikausgabe auf AGA-Screens ziemlich langsam
ist. Wird TinyDev auf einem AGA-Screen genutzt, kann es helfen,
die Anzahl der Farben des Workbench-Screens zu reduzieren, um die
Benutzeroberfläche zu beschleunigen.

Um auf langsamen Amigas die Ausführungsgeschwindigkeit weiter zu
erhöhen, können folgende Dinge ausprobiert werden:

- **Symbolleiste**  
  Das Laden der AISS-Symbole für die Symbolleiste benötigt etwas
  Zeit. Um den Programmstart zu beschleunigen, kann unter
  *Einstellungen / Werkzeugleiste* auf eine reine Text-Darstellung
  umgeschaltet werden.
- **Scrolling**  
  Wenn die Geschwindigkeit beim Scrollen im Text-Editor zu langsam
  ist, kann zunächst versucht werden, unter *Einstellungen / Allgemein*
  die Option &raquo;Hervorhebung beim Scrollen aussetzen&laquo;
  eingeschaltet werden.
- **Quelltext-Analyse**  
  TinyDev enthält mit Scout ein modulares und mächtiges System für die
  Quelltext-Analyse. Allerdings benötigt diese Analyse Zeit, vor allem
  auf langsamen Festplatten / Massenspeichern. Die Geschwindigkeit kann
  in drei Stufen erhöht werden:
  
  - Der Pfad für temporäre Dateien sollte auf einem schnellen Laufwerk
    liegen. Als Standardeinstellung wird `T:` verwendet. Dieses Assign
    sollte auf ein Verzeichnis in der RAM Disk zeigen.
  - Auch Dateien in den Include-Verzeichnissen werden analysiert.
    Bei großen Include-Sammlungen (bspw. im NDK von AmigaOS) kann es
    sinnvoll sein, diese unter *Projekteinstellungen / Include-Verzeichnisse*
    zu entfernen (falls sie dort aufgeführt sind).
  - Die Quellcode-Analyse mit Scout kann in *Einstellungen / Erweitert*
    auch vollständig abgeschaltet werden. Dann funktionieren allerdings
    Syntax-Highlighting, Symbolliste, Problemliste und andere Features nicht mehr.
    
- **Git-Integration**  
  Die Ausführung von am-git im Hintergrund, um den Dateistatus
  automatisch auf dem aktuellen Stand zu halten, benötigt relativ
  viel Zeit. Wenn TinyDev dadurch zu langsam wird, sollte die
  Git-Integration über *Einstellungen / Git* wieder ausgeschaltet
  werden. am-git kann dann immer noch über die Shell verwendet werden.
  
