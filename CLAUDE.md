# TinyDev – Hinweise für Claude

Projektbeschreibung, Aufbau und Konventionen stehen in `AGENTS.md`:

@AGENTS.md

Gemeinsame Regeln aller Amiga-Projekte (Submodule `amiga-devkit/`, gepinnt auf ein Tag):

@amiga-devkit/docs/Amiga-Regeln.md
@amiga-devkit/docs/ReAction-Styleguide.md
@amiga-devkit/docs/Dokumentation.md

Technische Notizen, die mehrere Module betreffen, stehen in `TECHNICAL.md` (nicht
automatisch geladen; `AGENTS.md` nennt unter „Technical notes“, welcher Abschnitt vor welcher
Änderung zu lesen ist).

## Abweichungen von den Devkit-Regeln

Wo `AGENTS.md` und die Devkit-Dokumente sich widersprechen, gilt Folgendes:

- **Ziel-CPU:** TinyDev wird für den 68000 übersetzt (`-m68000 -msoft-float -noixemul`), die
  Vorgabe `-m68020-60 -mtune=68060` gilt hier nicht. Genutzt wird es aber auf größeren Amigas:
  mindestens 68030/50 MHz, eher 68040 oder 68060. Laufzeit-Abwägungen (Suche, Parser, Minimap)
  daran ausrichten, nicht an einem 7-MHz-68000. Trotzdem keine `long long`-Arithmetik in heißen
  Schleifen.
- **Projekt:** Ein geöffnetes Verzeichnis (Wurzel des Verzeichnisbaums) gilt als Projekt.
  Funktionen mit Projektbezug (z. B. Suche in allen Projektdateien) beziehen sich darauf.
- **Dokumentation:** `make docs` (ganzes Programm) sowie `make docs-dirtree` und
  `make docs-minimap` (öffentliche Header der Klassen) erzeugen HTML unter `build/docs/` und
  scheitern bei jeder Doxygen-Warnung. Vor einem Commit, der Kommentare oder Schnittstellen
  ändert, laufen sie ohne Warnung (Doxygen muss auf dem Host installiert sein).
- **Host-Tests** heißen `make test` (nicht `make check`), ohne ASan/UBSan und ohne
  `.expected`-Referenzausgaben. Vor einem Commit: `make` (enthält `charcheck`) und `make test`.
- **Vorlagen:** Nach Änderungen unter `templates/` zusätzlich `make check-templates`: baut
  die C-Vorlagen mit vbcc (`/opt/vbcc`) und dem gcc-Cross-Compiler und überspringt, was nicht
  installiert ist. Kein Ersatz für den Test mit gcc 2.95.3 und auf dem Amiga.
- **Kataloge:** Sichtbare Texte von TinyDev (nicht von Scout und den Gadget-Klassen) kommen
  aus `catalogs/TinyDev.cd` über `S(MSG_…)` (`src/locale.c`); jeder neue Text bekommt dort
  einen Eintrag und eine Übersetzung in `catalogs/deutsch.ct`. Bis zum ersten Release dürfen
  Texte umsortiert werden, eine Katalogversion (`OC_Version`) gibt es erst dann.
  `include/tinydev_strings.h` wird erzeugt und eingecheckt. Kürzel (`_`) pro Fenster und
  Sprache eindeutig halten. Ausgaben auf `stderr` bleiben englisch.
- **Noch nicht vorhanden** (nur nach Absprache einführen): `README.de.md`. `package/` enthält
  die Aminet-Readme, das Installer-Skript (`Install`) und die Hilfeseiten
  (`package/help/<Sprache>/`), `make dist` baut das Archiv.
- **Installer:** Alles kommt in eine Schublade, auch die Kataloge (nicht nach `LOCALE:`, anders
  als der Styleguide vorsieht) und die Klassen: `help.c` und das Hilfe-Template erwarten
  HelpViewer und `mdtohtml` im Programmverzeichnis, und eine global installierte Klasse verdeckt
  die neuere daneben. Nur `mdtohtml` gibt es wahlweise zusätzlich in `C:`. Icons mit
  Einstellungen (TinyDev, TinyDev-Scout, HelpViewer) überschreibt ein Update nicht.
- **Hilfeseiten:** Die Texte unter `package/help/` schreibt der Benutzer selbst. Claude
  ändert sie nicht von sich aus, auch nicht für neue Funktionen: die deutschen gar nicht,
  die englischen nur, wenn der Benutzer um eine Übersetzung bittet. Im Bericht darf Claude
  darauf hinweisen, dass eine Funktion in der Hilfe noch fehlt.
- **README:** Die `README.md` pflegt der Benutzer und hält sie kurz. Claude ändert sie nur auf
  Ansage; neue Funktionen, Tooltypes und Grenzen bekommen dort keinen Eintrag, anders als die
  Devkit-Dokumente vorsehen. Die Credits (u. a. die von der Regex-Lizenz verlangte Nennung)
  bleiben drin.
- **Versionen:** Das erste Release von TinyDev und seinen Gadget-Klassen wird 1.0. Bis dahin
  lädt TinyDev die Klassen aus `bin/` (`PROGDIR:Classes/Gadgets/`).
- **Schrift:** Der Editor nutzt immer die Standard-Bildschirmschrift, eine Schriftwahl gibt es
  nicht (siehe `AGENTS.md`).
- **Zeichensatz:** Welche Dateien ISO-8859-1 sein müssen, legt `LATIN1` im `Makefile` fest
  (Quellen, Header, Tests, Skripte, `LICENSE`, `Makefile`). `AGENTS.md`, `CLAUDE.md` und
  `README.md` bleiben UTF-8.
