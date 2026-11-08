# Einführung

`TinyDev` ist ein erweiterter Editor, der in erster Linie für Programmierer
gedacht ist. Er wurde um das `texteditor`-Gadget aus AmigaOS 3.2 herum
entwickelt und benötigt deshalb mindestens AmigaOS 3.2. Auf älteren
Systemversionen funktioniert er nicht.

`TinyDev` bietet u. a. folgende Funktionen:

- ReAction-basierte Benutzeroberfläche mit Unterstützung für Übersetzungen  
(Englisch eingebaut; Deutsche Kataloge sind enthalten)
- Einstellungen pro Dateityp
- Integrierter Verzeichnis-Browser
- Minimap: eine farbige Übersicht des geöffneten Dokuments. Mit Klicken & Ziehen kann im Dokument gescrollt werden.
- Einfache Projektverwaltung
- Datei- und Projekt-Vorlagen
- Syntax-Highlighting für verschiedene Sprachen
- Lesezeichenverwaltung
- Statische Quellcode-Analyse
- Symbolliste
- Auto-Vervollständigung anhand gefundener Symbole  
(**keine** einfache Keyword-Liste)
- Suchen und Ersetzen über mehrere Dateien; Unterstützung für reguläre Ausdrücke
- Integrierte Dokumentation (in Arbeit)
    - Mit dem `HelpViewer` steht ein einfaches, aber funktionales
      Werkzeug für die Anzeige der mitgelieferten Dokumentation bereit
    - `TinyDev` enthält eine Projektvorlage und alle notwendigen Werkzeuge,
      um eigene Hilfeseiten zu erstellen und zu bearbeiten
- Erweiterbarkeit ohne Anpassung von `TinyDev`:
    - Eigene Vorlagen können ergänzt werden
    - Eigene Quellcode-Parser können ergänzt werden
- Wiederverwendbarkeit von Komponenten:
    - Die ReAction-Klassen `dirtree` und `minimap` können in eigenen ReAction-Anwendungen
      genutzt werden
    - Die Tools für die Quellcode-Analyse (Scout) sind so aufgebaut, dass sie in
      eigene Anwendungen über ein einfaches JSON-basiertes Protokoll eingebunden
      werden können
    - Der `HelpViewer` ist als allgemeines Werkzeug konzipiert. Seine Lizenz
      erlaubt die Einbettung in fremde Projekte


## Anforderungen

`TinyDev` benötigt AmigaOS 3.2. Auf älternen Systemversionen funktioniert
`TinyDev` nicht. Getestet wurde er ausschließlich auf AmigaOS 3.2.3.

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
Ein automatisches Starten dürfte ohnehin nicht sinnvoll sein. `TinyDev` startet
das Commodity bei Bedarf selbst (aus dem Anwendungsordner).
