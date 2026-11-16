# Scout

Die Komponenten für die Quelltext-Analyse - sowohl Scout als die
einzelnen Parser - können in eigene Anwendungen integriert werden.

Grundsätzlich gilt: die Parser untersuchen mit einem Programmaufruf
nur jeweils *eine* Datei. Scout dagegen kümmert sich um Sitzungen,
um Caches / Puffer usw. Eine IDE wird deshalb in der Regel mit dem
Scout-Commodity &raquo;reden&laquo;, bei einfacheren Anforderungen
kann es sinnvoll sein, direkt einen Parser aufzurufen.

In beiden Szenarien werden Anfragen und Antworten im JSON-Format
ausgetauscht, dementsprechend muss für eine Einbindung in eigene
Anwendungen mindestens ein JSON-Parser vorhanden sein.
