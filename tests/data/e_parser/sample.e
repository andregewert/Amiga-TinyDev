/* A sample for the AmigaE parser. /* Comments nest. */ Still one. */
OPT OSVERSION=37, LARGE

MODULE 'intuition/intuition', 'dos/dos',
       '*helper'

CONST MAX_ITEMS=10, BIG=$FF
ENUM ER_NONE, ER_MEM=5, ER_FILE
SET FLAG_A, FLAG_B

#define TWICE(x) ((x)*2)

OBJECT point
  x:INT, y:INT
  name[16]:ARRAY OF CHAR
ENDOBJECT

EXPORT OBJECT circle OF point
PRIVATE
  radius
PUBLIC
  next:PTR TO circle
  UNION
    [a:LONG, b]
    [
      c:CHAR
    ]
  ENDUNION
ENDOBJECT

DEF count = 0, list:PTR TO point, buffer[80]:STRING

PROC area(r=1) OF circle IS r * self.radius

PROC twice(a) RETURN TWICE(a)

EXPORT PROC sum(a, b:PTR TO LONG, c=0) HANDLE
  DEF i, total=0
  FOR i := 0 TO a DO total := total + b[i]
  FOR i := 0 TO 3
    IF i = c THEN JUMP done
    IF total > MAX_ITEMS
      Raise(ER_MEM)
    ELSEIF total = 0
      total++
    ELSE
      total--
    ENDIF
  ENDFOR
done:
  WHILE c < 2 AND
        a > 0
    INC c; DEC a
  ENDWHILE
  REPEAT
    SELECT c
      CASE 1; WriteF('one\n')
      DEFAULT
        WriteF('it''s \a other\n', "ab")
    ENDSELECT
  UNTIL CtrlC()
  LOOP
    EXIT total
  ENDLOOP
EXCEPT DO
  ReportBadThing(exception)
ENDPROC total, list.x

PROC main()
  DEF p:PTR TO circle, v
  NEW p
  v := SIZEOF point
  MOVE.L D0,(A1)+
  LEA data(PC),A0
  WriteF('\d\n', sum(1, [1, 2], twice(v)))
  END p
ENDPROC

data: INCBIN 'data.bin'
