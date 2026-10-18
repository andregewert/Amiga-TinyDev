# TinyDev – Hinweise für Claude

Projektbeschreibung, Aufbau und Konventionen stehen in `AGENTS.md`:

@AGENTS.md

Gemeinsame Regeln aller Amiga-Projekte (Submodule `amiga-devkit/`, gepinnt auf ein Tag):

@amiga-devkit/docs/Amiga-Regeln.md
@amiga-devkit/docs/ReAction-Styleguide.md

## Abweichungen von den Devkit-Regeln

Wo `AGENTS.md` und die Devkit-Dokumente sich widersprechen, gilt Folgendes:

- **Ziel-CPU:** TinyDev läuft auf dem 68000 (`-m68000 -msoft-float -noixemul`). Die Vorgabe
  `-m68020-60 -mtune=68060` gilt hier nicht; trotzdem keine `long long`-Arithmetik in heißen
  Schleifen.
- **Host-Tests** heißen `make test` (nicht `make check`), ohne ASan/UBSan und ohne
  `.expected`-Referenzausgaben. Vor einem Commit: `make` (enthält `charcheck`) und `make test`.
- **Noch nicht vorhanden** (nur nach Absprache einführen): Kataloge/`locale.library`,
  `README.de.md`, `package/` mit Aminet-Readme, `make dist`, Einstellungsfenster
  und das Schreiben von Tooltypes (gelesen werden sie in `src/settings.c`).
  Bis dahin stehen sichtbare Texte noch im Code; neue Texte möglichst so
  anlegen, dass sie später leicht in einen Katalog wandern.
- **Versionen:** Das erste Release von TinyDev und seinen Gadget-Klassen wird 1.0. Bis dahin
  lädt TinyDev die Klassen aus `bin/` (`PROGDIR:Classes/Gadgets/`).
- **Schrift:** Der Editor nutzt immer die Standard-Bildschirmschrift, eine Schriftwahl gibt es
  nicht (siehe `AGENTS.md`).
- **Zeichensatz:** Welche Dateien ISO-8859-1 sein müssen, legt `LATIN1` im `Makefile` fest
  (Quellen, Header, Tests, Skripte, `LICENSE`, `Makefile`). `AGENTS.md`, `CLAUDE.md`,
  `README.md` und `FUTURE.md` bleiben UTF-8.
