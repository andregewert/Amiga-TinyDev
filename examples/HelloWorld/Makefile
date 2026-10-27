# Makefile of the HelloWorld demo project.
#
# Needs vbcc (the command vc, set up for AmigaOS 68k) and a make for
# AmigaOS (GNU make); the commands run in the AmigaShell.

CC = vc
CFLAGS = -O1

hello: hello.c
	$(CC) $(CFLAGS) -o hello hello.c

clean:
	-Delete hello QUIET

.PHONY: clean
