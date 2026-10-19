# TinyDev – Hinweise für Claude

Projektbeschreibung, Aufbau und Konventionen stehen in `AGENTS.md`:

@AGENTS.md

Gemeinsame Regeln aller Amiga-Projekte (Submodule `amiga-devkit/`, gepinnt auf ein Tag):

@amiga-devkit/docs/Amiga-Regeln.md
@amiga-devkit/docs/ReAction-Styleguide.md

## Abweichungen von den Devkit-Regeln

Wo `AGENTS.md` und die Devkit-Dokumente sich widersprechen, gilt Folgendes:

- **Ziel-CPU:** TinyDev wird für den 68000 übersetzt (`-m68000 -msoft-float -noixemul`), die
  Vorgabe `-m68020-60 -mtune=68060` gilt hier nicht. Genutzt wird es aber auf größeren Amigas:
  mindestens 68030/50 MHz, eher 68040 oder 68060. Laufzeit-Abwägungen (Suche, Parser, Minimap)
  daran ausrichten, nicht an einem 7-MHz-68000. Trotzdem keine `long long`-Arithmetik in heißen
  Schleifen.
- **Projekt:** Ein geöffnetes Verzeichnis (Wurzel des Verzeichnisbaums) gilt als Projekt.
  Funktionen mit Projektbezug (z. B. Suche in allen Projektdateien) beziehen sich darauf.
- **Host-Tests** heißen `make test` (nicht `make check`), ohne ASan/UBSan und ohne
  `.expected`-Referenzausgaben. Vor einem Commit: `make` (enthält `charcheck`) und `make test`.
- **Kataloge:** Sichtbare Texte von TinyDev (nicht von Scout und den Gadget-Klassen) kommen
  aus `catalogs/TinyDev.cd` über `S(MSG_…)` (`src/locale.c`); jeder neue Text bekommt dort
  einen Eintrag und eine Übersetzung in `catalogs/deutsch.ct`. Bis zum ersten Release dürfen
  Texte umsortiert werden, eine Katalogversion (`OC_Version`) gibt es erst dann.
  `include/tinydev_strings.h` wird erzeugt und eingecheckt. Kürzel (`_`) pro Fenster und
  Sprache eindeutig halten. Ausgaben auf `stderr` bleiben englisch.
- **Noch nicht vorhanden** (nur nach Absprache einführen): `README.de.md`, `package/` mit
  Aminet-Readme, `make dist`.
- **Versionen:** Das erste Release von TinyDev und seinen Gadget-Klassen wird 1.0. Bis dahin
  lädt TinyDev die Klassen aus `bin/` (`PROGDIR:Classes/Gadgets/`).
- **Schrift:** Der Editor nutzt immer die Standard-Bildschirmschrift, eine Schriftwahl gibt es
  nicht (siehe `AGENTS.md`).
- **Zeichensatz:** Welche Dateien ISO-8859-1 sein müssen, legt `LATIN1` im `Makefile` fest
  (Quellen, Header, Tests, Skripte, `LICENSE`, `Makefile`). `AGENTS.md`, `CLAUDE.md`,
  `README.md` und `FUTURE.md` bleiben UTF-8.
