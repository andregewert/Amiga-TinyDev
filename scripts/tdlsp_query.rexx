/* $VER: tdlsp_query.rexx 1.0 (19.09.2026) TinyDev LSP Query Example */
OPTIONS RESULTS

PARSE ARG cmd file line col prefix

IF cmd = "" THEN DO
    SAY "TinyDev-LSP ARexx Query Client"
    SAY "Usage: rx tdlsp_query.rexx <COMMAND> [FILE] [LINE] [COL] [PREFIX]"
    SAY "Commands: STATUS, PARSE, SYMBOLS, DIAGNOSTICS, DEFINITION, COMPLETE, QUIT"
    EXIT 0
END

port = 'TINYDEV_LSP'

IF ~SHOW('P', port) THEN DO
    SAY "Error: Port" port "not found. Is TinyDev-LSP running?"
    EXIT 10
END

ADDRESS VALUE port

SELECT
    WHEN UPPER(cmd) = 'STATUS' THEN DO
        'STATUS'
        SAY "Result (" || RC || "):" RESULT
    END

    WHEN UPPER(cmd) = 'PARSE' THEN DO
        IF file = "" THEN file = "src/main.c"
        'PARSE' file
        SAY "PARSE" file "->" RC ":" RESULT
    END

    WHEN UPPER(cmd) = 'SYMBOLS' THEN DO
        IF file = "" THEN file = "src/main.c"
        'SYMBOLS' file
        SAY "SYMBOLS for" file ":"
        SAY RESULT
    END

    WHEN UPPER(cmd) = 'DIAGNOSTICS' THEN DO
        IF file = "" THEN file = "src/main.c"
        'DIAGNOSTICS' file
        SAY "DIAGNOSTICS for" file ":"
        SAY RESULT
    END

    WHEN UPPER(cmd) = 'DEFINITION' THEN DO
        IF file = "" THEN file = "src/main.c"
        IF line = "" THEN line = "1"
        IF col = "" THEN col = "1"
        'DEFINITION' file line col prefix
        SAY "DEFINITION:" RESULT
    END

    WHEN UPPER(cmd) = 'COMPLETE' THEN DO
        IF file = "" THEN file = "src/main.c"
        IF line = "" THEN line = "1"
        IF col = "" THEN col = "1"
        'COMPLETE' file line col prefix
        SAY "COMPLETIONS for prefix '" || prefix || "':"
        SAY RESULT
    END

    WHEN UPPER(cmd) = 'QUIT' THEN DO
        'QUIT'
        SAY "QUIT sent to TinyDev-LSP ->" RC
    END

    OTHERWISE DO
        SAY "Unknown command:" cmd
    END
END

EXIT RC
