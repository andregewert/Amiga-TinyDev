# Makefile of {$NAME}.
#
# Needs vbcc (the command vc, set up for AmigaOS 68k) and a make for
# AmigaOS (GNU make); the commands run in the AmigaShell.

CC = vc
CFLAGS = -O1

{$NAME}: {$NAME}.c
	$(CC) $(CFLAGS) -o {$NAME} {$NAME}.c

clean:
	-Delete {$NAME} QUIET

.PHONY: clean
