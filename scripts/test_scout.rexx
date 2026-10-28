/* $VER: test_scout.rexx 1.0 (28.10.2026) TinyDev-Scout test run */
/*
 * Tests the commands of TinyDev-Scout with the sample sources in
 * tests/data/c_parser/ and shows PASS or FAIL for each.
 *
 * Run it from the TinyDev directory (with bin/ built):
 *   rx scripts/test_scout.rexx         leaves Scout running afterwards
 *   rx scripts/test_scout.rexx QUIT    ends Scout at the end
 * If Scout does not run, the script starts bin/TinyDev-Scout.
 */
OPTIONS RESULTS
PARSE UPPER ARG option

port = 'TINYDEV_SCOUT'
pass = 0
fail = 0

cwd = PRAGMA('D')
IF RIGHT(cwd, 1) ~= ':' THEN cwd = cwd || '/'
data = cwd || 'tests/data/c_parser/'
sample = data || 'sample.c'
header = data || 'sample.h'

IF ~SHOW('P', port) THEN DO
    SAY 'Starting bin/TinyDev-Scout ...'
    ADDRESS COMMAND 'Run >NIL: bin/TinyDev-Scout'
    ADDRESS COMMAND 'WaitForPort' port
    IF ~SHOW('P', port) THEN DO
        SAY 'TinyDev-Scout did not start.'
        EXIT 10
    END
END

/* files the tests need */
IF OPEN('f', 'T:scout_snap.c', 'W') THEN DO
    CALL WRITELN 'f', 'int snapshot_only(void) { return 0; }'
    CALL CLOSE 'f'
END
IF OPEN('f', 'T:scout_broken.c', 'W') THEN DO
    CALL WRITELN 'f', 'int broken(void) {'
    CALL WRITELN 'f', '    char *s = "open;'
    CALL CLOSE 'f'
END

SAY '--- Scout and its parsers'
/* LANGUAGES waits until Scout has read its parsers (just after its start) */
CALL Check 'LANGUAGES', 'LANGUAGES', '"id":"c"'
CALL Check 'STATUS', 'STATUS', '"name":"c_parser"'
CALL Check 'LANGUAGE of a .c file', 'LANGUAGE FILE x.c', '"language":"c"'
CALL Check 'LANGUAGE of a .txt file', 'LANGUAGE FILE x.txt', '"language":null'
CALL Check 'SYNTAX of C', 'SYNTAX LANG c', '"lineComments":'

SAY '--- Single files (session 0)'
CALL Check 'ANALYZE', 'ANALYZE FILE "' || sample || '"', '"symbols":17'
CALL Check 'SYMBOLS of a file', 'SYMBOLS FILE "' || sample || '"', '"name":"helper"'
CALL Check 'TOKENS into a file', 'TOKENS FILE "' || sample || '" TO T:scout_tokens.json', '"size":'
CALL Check 'DIAGNOSTICS of a broken file', 'DIAGNOSTICS FILE T:scout_broken.c', 'Unterminated string literal'

SAY '--- A project (session)'
sid = 0
IF Check('OPEN', 'OPEN "' || data || '"', '"session":') THEN PARSE VAR last '{"session":' sid '}'
SAY '     session' sid
/* wait until the project is indexed (at most 20 seconds) */
DO i = 1 TO 20
    ADDRESS VALUE port
    'STATUS'
    status = RESULT
    PARSE VAR status '"queue":' queue ','
    IF queue = 0 THEN LEAVE
    ADDRESS COMMAND 'Wait 1'
END
SAY '     indexed (queue' queue || ')'

CALL Check 'SYMBOLS of the project', 'SYMBOLS SESSION' sid 'NAME add#?', '"name":"add_numbers"'
CALL Check 'DEFINITION of a macro in the header', 'DEFINITION SESSION' sid 'FILE "' || sample || '" LINE 17 COLUMN 16', 'sample.h'
CALL Check 'DEFINITION of the word at a position', 'DEFINITION SESSION' sid 'FILE "' || sample || '" LINE 28 COLUMN 20', '"name":"helper"'
CALL Check 'DEFINITION of a local', 'DEFINITION SESSION' sid 'FILE "' || sample || '" LINE 20 COLUMN 10 NAME mask', '"start":[12,11]'
CALL Check 'COMPLETE the word before a position', 'COMPLETE SESSION' sid 'FILE "' || sample || '" LINE 20 COLUMN 11', '"label":"mask"'
CALL Check 'COMPLETE keywords', 'COMPLETE SESSION' sid 'FILE "' || sample || '" LINE 29 COLUMN 5 PREFIX wh', '"label":"while","keyword":true'
CALL Check 'DIAGNOSTICS of the project', 'DIAGNOSTICS SESSION' sid, '{"files":[]}'

SAY '--- A snapshot of the editor'
CALL Check 'ANALYZE a snapshot', 'ANALYZE SESSION' sid 'FILE "' || sample || '" TEXT T:scout_snap.c VERSION 2', '"version":2'
CALL Check 'SYMBOLS of the snapshot', 'SYMBOLS SESSION' sid 'FILE "' || sample || '"', 'snapshot_only'
CALL Check 'FORGET the snapshot', 'FORGET SESSION' sid 'FILE "' || sample || '"', '{}'
CALL Check 'SYMBOLS of the file again', 'SYMBOLS SESSION' sid 'FILE "' || sample || '"', '"name":"helper"'

SAY '--- Errors'
CALL Fails 'missing file', 'ANALYZE FILE "' || data || 'missing.c"', 6
CALL Fails 'no parser for the file', 'ANALYZE FILE "' || cwd || 'README.md"', 4
CALL Fails 'unknown session', 'SYMBOLS SESSION 99', 2
CALL Fails 'unknown command', 'BOGUS', 1
CALL Fails 'missing argument', 'TOKENS', 1
CALL Fails 'unknown language', 'SYNTAX LANG cobol', 4

SAY '--- End'
CALL Check 'CLOSE', 'CLOSE' sid, '{}'
CALL Fails 'CLOSE again', 'CLOSE' sid, 2
IF option = 'QUIT' THEN CALL Check 'QUIT', 'QUIT', '{}'

ADDRESS COMMAND 'Delete >NIL: T:scout_snap.c T:scout_broken.c T:scout_tokens.json QUIET'
SAY ''
SAY pass 'passed,' fail 'failed'
IF fail > 0 THEN EXIT 5
EXIT 0

/*
 * Check(title, command, expected): sends a command and expects success and
 * a result containing the expected text. The result is left in "last".
 */
Check: PROCEDURE EXPOSE port pass fail last
    PARSE ARG title, command, expected
    TRACE OFF   /* no "+++ Command returned 10" for failed commands */
    last = ''
    ADDRESS VALUE port
    command
    IF RC = 0 THEN DO
        last = RESULT
        IF POS(expected, last) > 0 THEN DO
            pass = pass + 1
            SAY 'PASS' title
            RETURN 1
        END
        fail = fail + 1
        SAY 'FAIL' title '- expected' expected
        SAY '     got' LEFT(last, 300)
        RETURN 0
    END
    'ERROR'
    fail = fail + 1
    SAY 'FAIL' title '- error' RESULT
    RETURN 0

/*
 * Fails(title, command, code): sends a command and expects it to fail with
 * the given error number (read with ERROR).
 */
Fails: PROCEDURE EXPOSE port pass fail
    PARSE ARG title, command, code
    TRACE OFF   /* no "+++ Command returned 10" for the expected failures */
    ADDRESS VALUE port
    command
    IF RC = 0 THEN DO
        fail = fail + 1
        SAY 'FAIL' title '- succeeded:' LEFT(RESULT, 200)
        RETURN 0
    END
    'ERROR'
    error = RESULT
    IF POS('"code":' || code || ',', error) > 0 THEN DO
        pass = pass + 1
        SAY 'PASS' title '(error' code || ')'
        RETURN 1
    END
    fail = fail + 1
    SAY 'FAIL' title '- expected error' code || ', got' error
    RETURN 0
