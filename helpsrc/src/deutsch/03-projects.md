# Projektverwaltung

TinyDev betrachtet grundsätzlich jedes Verzeichnis als Projekt,
d. h. jedes existierende Verzeichnis kann *als Projekt* geöffnet
werden. Es müssen keine TinyDev-spezifischen Projektdateien vorhanden
sein. Allerdings legt TinyDev (ungefragt) einen Unterordner
`.tinydev` und bestimmte Dateien an, wenn projektbezogene Einstellungen
(wie bspw. Lesezeichen) gespeichert werden.

Sobald ein Verzeichnis bzw. Projekt geöffnet worden ist, zeigt der
Verzeichnisbaum den zugehörigen Inhalt an, wobei standardmäßig `.info`-Dateien
ausgeblendet werden. Außerdem steht nun der Menüpunkt
*Projekt / Projekteinstellungen* zur Verfügung.

## Ein neues Projekt anlegen

Über den Menüpunkt *Projekt / Neues Projekt...* wird der Dialog für die
Erstellung eines neuen Projektes geöffnet. Der Dialog erlaubt die Auswahl
einer Projektvorlage einschließlich der Auswahl eine Zielverzeichnisses.

Ist die Git-Integration eingeschaltet, lässt sich auf Wunsch das anzulegende
Projektverzeichnis als Git-Repository initialisieren.

Auf der zweiten Dialogseite werden - abhängig von der Vorlage - verschiedene
Optionen abgefragt.

## Projekteinstellungen

Im Dailog *Projekteinstellungen* (Menü: Projekt / Projekteinstellungen...)
stehen folgende Einstellungen zur Verfügung:

### Projektname

Der Projektname kann von Vorlagen ausgewertet werden und wird von TinyDev
im *AppIcon* angezeigt, wenn die Anwendung ikonifiziert wird.

### Erstellen, Ausführen, Debuggen

Die Einstellungen für diese drei Funktionen gleichen sich - der Unterschied
liegt darin, dass jede der drei Aktionen einen eigenen Befehl in Symbolleiste,
im Menü und in den Tastaturkürzeln besitzt.

Für jede Aktion können AmigaDOS-Befehle hinterlegt werden, die beim Auslösen
der Aktion ausgeführt werden. Mehrere Befehle müssen in jeweils einer
einzelnen Zeile notiert werden. Die mitgeleiferten Projektvorlagen liefern
gute Beispiele für mögliche Skripte.

Es können jeweils mehrere Befehle für die Aktionen definiert werden und
jeweils ein Standardwert festgelegt werden. Für die Aktion &raquo;Erstellen&ldquo;
ist es beispielsweise üblich, zwischen Release Build und Debug Build zu
unterscheiden. Ein schneller Wechsel zwischen den Einträgen ist über das
Menü *Erstellen* möglich.

### Include-Verzeichnisse

An dieser Stelle können (und sollten!) Include-Verzeichnisse hinterlegt
werden, die außerhalb des Projektes liegen und zusätzlich von Scout
ausgewertet werden sollen, um bspw. Vorschläge für die Code-Vervollständigung
machen zu können.

Bei C-Projekten ist es sinnvoll, hier die Compiler-spezifischen
Include-Verzeichnisse sowie das Verzeichnis `Include_H` des NDK zu
hinterlegen. Zu beachten ist, dass die Quelltext-Analyse des NDK-Verzeichnisses
auf langsamen Amiga eine Weile dauern kann.

### Git
...
