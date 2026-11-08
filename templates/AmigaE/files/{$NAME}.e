/*
 * {$NAME}.e - {$PURPOSE}.
 *
 * Copyright (c) {$YEAR} {$AUTHOR}
 * {$AUTHORMAIL}
 * Created {$DATE}
 */

OPT OSVERSION=37

CONST DEFAULT_TIMES=1

/* Print the greeting a number of times. */
PROC greet(who,times)
  DEF i
  FOR i:=1 TO times DO WriteF('Hello, \s!\n',who)
ENDPROC

PROC main() HANDLE
  DEF rdargs=NIL,args:PTR TO LONG,who,times
  args:=[NIL,NIL]
  rdargs:=ReadArgs('NAME,TIMES/N',args,NIL)
  IF rdargs=NIL THEN Raise("ARGS")
  who:=IF args[0] THEN args[0] ELSE 'World'
  times:=IF args[1] THEN Long(args[1]) ELSE DEFAULT_TIMES
  greet(who,times)
EXCEPT DO
  IF rdargs THEN FreeArgs(rdargs)
  IF exception="ARGS" THEN WriteF('Usage: {$NAME} [NAME] [TIMES]\n')
  IF exception THEN CleanUp(10)
ENDPROC
