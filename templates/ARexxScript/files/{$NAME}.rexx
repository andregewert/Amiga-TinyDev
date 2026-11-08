/*
 * {$NAME}.rexx - {$PURPOSE}.
 *
 * Copyright (c) {$YEAR} {$AUTHOR}
 * {$AUTHORMAIL}
 * Created {$DATE}
 *
 * Usage: rx {$NAME}.rexx [name] [times]
 */
OPTIONS RESULTS

SIGNAL ON SYNTAX
SIGNAL ON ERROR
SIGNAL ON HALT
SIGNAL ON BREAK_C
SIGNAL ON NOVALUE

PARSE ARG who times .
IF who = '' THEN who = 'World'
IF times = '' THEN times = 1
IF ~DATATYPE(times, 'Numeric') THEN DO
    SAY 'Usage: rx {$NAME}.rexx [name] [times]'
    EXIT 5
END

CALL Greet who, times
EXIT 0

/* Say the greeting a number of times. */
Greet: PROCEDURE
    PARSE ARG name, count
    DO i = 1 TO count
        SAY 'Hello,' name || '!'
    END
    RETURN

SYNTAX:
    SAY 'Error in line' SIGL || ':' ERRORTEXT(RC)
    EXIT 20

ERROR:
    SAY 'A command in line' SIGL 'failed with return code' RC
    EXIT 10

NOVALUE:
    SAY 'A variable without a value in line' SIGL
    EXIT 10

HALT:
BREAK_C:
    SAY 'Stopped.'
    EXIT 5
