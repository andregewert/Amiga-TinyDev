# Einführung

TinyDev ist eine einfache Entwicklungsumgebung für Programmierer unter
AmigaOS 3.2, gebaut mit ReAction.

## Anforderungen

TinyDev benötigt AmigaOS 3.2. Auf älternen Systemversionen funktioniert
TinyDev nicht. Getestet wurde er ausschließlich auf AmigaOS 3.2.3.

Für die Icons in der Symbolleiste wird [AISS](http://aminet.net/package/util/misc/aiss46)
oder [AISS Classic](http://aminet.net/package/util/misc/aissclassic)
benötigt. Ohne AISS funktioniert TinyDev, zeigt in der Symbolleiste aber keine
Bilder an.

Für den `HelpViewer` ist die ReAction-Klasse `html.gadget` erforderlich.
Sie ist im Aminet [verfügbar](http://aminet.net/package/dev/gui/html_gadget)
oder über seine [github-Seite](https://github.com/andregewert/Amiga-HTML-Gadget).

Sollen Internet-Links aus dem `HelpViewer` geöffnet werden, ist dafür
[OpenURL](http://aminet.net/package/comm/www/OpenURL-7.18) erforderlich.

TinyDev selbst enthält keine Compiler, keine Build-Werkzeuge und keinen
Debugger. Diese Werkzeuge müssen separat installiert werden.
Andererseits bringt TinyDev bereits fertige Projektvorlagen für einige
Compilersysteme mit, sodass bei Vorhandensein dieser Systeme direkt
*losgelegt* werden kann.
