' Sample of ACE BASIC for the tests of the parser.
REM a comment with IF in it
#include <ace/acedef.h>
#include "local.h"
#define LIMIT 10

CONST maxitems = 16, minitems = 1
{ a block
  comment IF }
/* another /* nested */ one */

DECLARE FUNCTION LONGINT AllocMem&(LONGINT size, LONGINT flags) LIBRARY exec
DECLARE SUB Helper(SHORTINT n%)
DECLARE STRUCT Point *origin
EXTERNAL FUNCTION LONGINT Peer

GLOBAL LONGINT counter

STRUCT Point
  SHORTINT x
  SHORTINT y
  STRING title SIZE 32
END STRUCT

CLASS Shape EXTENDS Point
  LONGINT area
END CLASS

METHOD LONGINT Area(Shape self)
  Area = self->area
END METHOD

LONGINT total, other
STRING buffer SIZE 80
DIM SHORTINT table(maxitems)
ATOM kind = #:circle

DEF FNtwice(v) = v * 2

SUB Helper(SHORTINT n%)
  SHARED total
  LONGINT i
  FOR i = 1 TO n%
    IF i > LIMIT THEN
      total = total + i
    ELSEIF i = 0 THEN
      total = 0
    ELSE
      total = total - FNtwice(i)
    END IF
  NEXT
END SUB

SUB LONGINT Twin&(a&) EXTERNAL
  Twin& = a& + 1
END SUB

SUB STRING Twin$(a$) EXTERNAL
  Twin$ = a$
END SUB

10 start:
  IF total > 0 THEN PRINT "yes" ELSE PRINT "no"
  WHILE total < maxitems
    total = total + ~
      1 : Helper(2)
  WEND
  REPEAT
    total = total - 1
  UNTIL total = 0
  CASE
    total = 1 : PRINT "one"
    ELSE PRINT "other"
  END CASE
  TYPECASE kind
    CASE Shape
      PRINT "shape"
    CASE ELSE
      PRINT "else"
  END TYPECASE
  ASSEM
    move.l  d0,d1  ; IF
  END ASSEM
  PRINT#1, "file"
  GOTO start
END
