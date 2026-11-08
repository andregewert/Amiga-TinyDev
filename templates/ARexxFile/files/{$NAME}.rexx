/*
 * {$NAME} - {$PURPOSE}
 * 
 * Project: {$PROJECT}
 * Copyright (c) {$YEAR} {$AUTHOR}
 * {$AUTHORMAIL}
 * Created {$DATE}
 */
OPTIONS RESULTS

SIGNAL ON SYNTAX
SIGNAL ON ERROR
SIGNAL ON HALT
SIGNAL ON BREAK_C

PARSE ARG arguments

EXIT 0

SYNTAX:
    SAY 'Error in line' SIGL || ':' ERRORTEXT(RC)
    EXIT 20

ERROR:
    SAY 'A command in line' SIGL 'failed with return code' RC
    EXIT 10

HALT:
BREAK_C:
    SAY 'Stopped.'
    EXIT 5
