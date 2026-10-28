/* $VER: tdscout_query.rexx 1.1 (18.10.2026) TinyDev Scout Query Example */
/*
 * Sends one command to TinyDev-Scout and shows its result (JSON) or error.
 * Usage: rx tdscout_query.rexx <command and arguments>
 * e.g.   rx tdscout_query.rexx STATUS
 *        rx tdscout_query.rexx ANALYZE FILE Work:Hello/hello.c
 *        rx tdscout_query.rexx OPEN Work:Hello
 *        rx tdscout_query.rexx SYMBOLS SESSION 1
 *        rx tdscout_query.rexx DEFINITION SESSION 1 FILE Work:Hello/hello.c LINE 5 COLUMN 3
 */
OPTIONS RESULTS
PARSE ARG command

IF command = "" THEN DO
    SAY "Usage: rx tdscout_query.rexx <command and arguments>"
    SAY "Commands: OPEN, CLOSE, INCLUDE, ERROR, ANALYZE, FORGET, SYMBOLS, DIAGNOSTICS,"
    SAY "          TOKENS, DEFINITION, COMPLETE, LANGUAGES, LANGUAGE, SYNTAX, REGISTER,"
    SAY "          RESCAN, STATUS, QUIT (see src/scout/docs/Scout.md)"
    EXIT 0
END

port = 'TINYDEV_SCOUT'
IF ~SHOW('P', port) THEN DO
    SAY "Error: port" port "not found. Is TinyDev-Scout running?"
    EXIT 10
END

ADDRESS VALUE port
command
IF RC = 0 THEN DO
    SAY RESULT
    EXIT 0
END
/* the error's number and text */
'ERROR'
SAY "Failed:" RESULT
EXIT 10
