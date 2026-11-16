# Dateien bearbeiten

## Allgemeines

Dateien können - auch ohne dass ein Projekt angelegt werden muss -
geöffnet und bearbeitet werden. Die Verwendung der integrierten
[Projektverwaltung](03-projects.md) ist optional.

Sofern die Quelltext-Analyse eingeschaltet ist, versucht TinyDev,
den Dateityp automatisch zu bestimmen, um so Syntax-Hervorhebung
und andere Funktionen bereitzustellen. Ist die Quelltext-Analyse
ausgeschaltet, werden alle Dateien als *plain text* behandelt und
erweiterte Funktionen wie Symbolliste und Problemliste sind nicht
verfügbar.

## Wichtige Tastaturkürzel
...

## Dateien durchsuchen
...

## Statuszeile

In der Statuszeile werden folgende Informationen angezeigt
(von links nach recht):

- Anzahl der geöffneten Dokumente
- Anzahl der Zeilen im angezeigten Dokument
- Anzahl der vom Parser gefundenen Symboldefinitionen im angezeigten Dokument
- Anzahl der vom Parser gefundenen Probleme im angezeigten Dokument
- Art der Einrückung (Leerzeichen / Tabs)
- Eingestellter Dateityp bzw. verwendeter Parser
- Art der Zeilenenden (LF oder CRLF)

In der rechten unteren Ecke zeigt ein Ampelfarben-Symbol den aktuellen
Status von Scout:

- Grün: Scout ist bereit und wartet auf Befehle
- Gelb: Scout ist aktuell beschäftigt
- Rot: Scout ist nicht erreichbar bzw. nicht gestartet
- Grau: Die Scout-Nutzung wurde in den Einstellungen abgeschaltet
