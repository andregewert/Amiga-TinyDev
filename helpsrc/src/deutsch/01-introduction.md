# Einführung

TinyDev ist ein erweiterter Editor, der in erster Linie für Programmierer
gedacht ist. Er wurde um das `texteditor`-Gadget aus AmigaOS 3.2 herum
entwickelt und benötigt deshalb mindestens AmigaOS 3.2. Auf älteren
Systemversionen funktioniert er nicht.

TinyDev bietet u. a. folgende Funktionen:

- ReAction-basierte Benutzeroberfläche mit Unterstützung für Übersetzungen
- Einstellungen pro Dateityp
- Integrierter Verzeichnis-Browser
- Minimap: eine farbige Übersicht des Dokuments; ein Klick oder Ziehen
  darin scrollt das Dokument.
- Einfache Projektverwaltung
- Datei- und Projekt-Vorlagen
- Syntax-Highlighting und einfache Code-Analyse für verschiedene Sprachen
  Aktuell: ACE BASIC, AmigaDOS, AmigaE, AmigaGuide, ARexx, Assembler, C,
  Free Pascal, HTML, JSON, Makefile, Markdown, XML
- Zwei Bitmap-Schriften mit fester Zeichenbreite für den Bildschirm:
  Holstein (Größen 16 und 17, abgeleitet von DejaVu Sans Mono) und
  Tamzen (15 und 16)
- Lesezeichenverwaltung
- Statische Quellcode-Analyse
- Symbolliste
- Auto-Vervollständigung anhand gefundener Symbole  
(**keine** einfache Keyword-Liste)
- Suchen und Ersetzen in mehreren Dokumenten; Unterstützung für reguläre Ausdrücke
- Gehe zur Definition, eingebundene Dateien öffnen, Problemliste
- Befehle zum Erstellen, Ausführen und Debuggen pro Projekt, mit
  Ausgabefenster
- Projektvorlagen für C (vbcc, gcc, SAS/C), Assembler (vasm, BAsm),
  AmigaE, Free Pascal, ACE BASIC und ARexx; die Vorlagen für C, E und
  Assembler starten einen Debug-Build in BDebug (Barfly), EDBG oder
  CodeProbe
- Git-Integration mit am-git (experimentell); Status im Verzeichnisbaum,
  Commit, Zweige, Verlauf, Tags, Fetch, Pull und Push
- Integrierte Dokumentation (in Arbeit)
    - Der `HelpViewer` ist ein einfaches, aber funktionales
      Werkzeug für die Anzeige der mitgelieferten Dokumentation
    - TinyDev enthält eine Projektvorlage und alle notwendigen Werkzeuge,
      um eigene Hilfeseiten zu erstellen und zu bearbeiten
- Erweiterbarkeit ohne Anpassung von TinyDev:
    - Eigene Vorlagen können ergänzt werden
    - Eigene Quellcode-Parser können ergänzt werden
- Wiederverwendbare Komponenten:
    - Die ReAction-Klassen `dirtree` und `minimap` können in eigenen ReAction-Anwendungen
      genutzt werden
    - Die Werkzeuge für die Quellcode-Analyse (Scout) sind so aufgebaut, dass sie
      über ein einfaches Protokoll (ARexx und JSON) in eigene Anwendungen
      eingebunden werden können
    - Der `HelpViewer` ist als allgemeines Werkzeug konzipiert. Seine Lizenz
      erlaubt die Einbettung in fremde Projekte


## Anforderungen

TinyDev benötigt AmigaOS 3.2. Auf älternen Systemversionen funktioniert
TinyDev nicht. Getestet wurde er ausschließlich auf AmigaOS 3.2.3.

Für die Icons in der Symbolleiste wird [AISS](http://aminet.net/package/util/misc/aiss46)
oder [AISS Classic](http://aminet.net/package/util/misc/aissclassic)
benötigt. Ohne AISS funktioniert `TinyDev`, zeigt in der Symbolleiste aber keine
Bilder an.

Für den `HelpViewer` ist die ReAction-Klasse `html.gadget` erforderlich.
Sie ist im Aminet [verfügbar](http://aminet.net/package/dev/gui/html_gadget)
oder über seine [github-Seite](https://github.com/andregewert/Amiga-HTML-Gadget).

Sollen Internet-Links aus dem `HelpViewer` geöffnet werden, ist dafür
[OpenURL](http://aminet.net/package/comm/www/OpenURL-7.18) erforderlich.


## Installation

Für die Installation liegt ein Installationsskript vor. Alle benötigten Komponenten
werden in das Zielverzeichnis installiert. Optional kann eine Kopie des
Markdown-Konverters `mdtohtml` nach `c:` kopiert werden.

Kataloge, Gadgets und Parser werden aus dem Anwendungsordner geladen und müssen
nicht auf die Systempartition kopiert werden. Auch das Commodity für die
Quellcode-Analyse (`TinyDev-Scout`) muss nicht nach `WBStartup` kopiert werden.
Ein automatisches Starten dürfte ohnehin nicht sinnvoll sein. TinyDev startet
das Commodity bei Bedarf selbst (aus dem Anwendungsordner).

Der Installer modifiziert nicht die `user-startup`. TinyDev benötigt keine
eigenen Assigns.
