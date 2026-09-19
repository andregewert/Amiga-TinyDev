/* $VER: test_lsp_broker.rexx 1.0 (19.09.2026) Automated ARexx Test Suite for TinyDev-LSP */
OPTIONS RESULTS

/*
 * Test Suite for TinyDev-LSP Commodity Broker
 *
 * This script tests all ARexx commands supported by TinyDev-LSP against
 * the actual TinyDev project source files.
 *
 * Requirements:
 * - TinyDev-LSP must be running (e.g. started via 'TinyDev-LSP' or Exchange)
 * - c_parser executable must be in PROGDIR:parsers/c_parser or build/parsers/c_parser
 */

port = 'TINYDEV_LSP'
passed = 0
failed = 0
total = 0

SAY "============================================================"
SAY " TinyDev-LSP ARexx Automated Test Suite"
SAY "============================================================"

IF ~SHOW('P', port) THEN DO
    SAY "ERROR: ARexx port '" || port || "' not found!"
    SAY "Please start TinyDev-LSP before running this test script."
    EXIT 20
END

ADDRESS VALUE port

/* Helper routine to evaluate test condition */
call_test:
    PARSE ARG test_name, expected_rc, result_contains

    total = total + 1
    actual_rc = RC
    actual_result = RESULT

    ok = 1
    IF expected_rc ~= "" THEN DO
        IF actual_rc ~= expected_rc THEN ok = 0
    END

    IF result_contains ~= "" THEN DO
        IF POS(result_contains, actual_result) = 0 THEN ok = 0
    END

    IF ok THEN DO
        passed = passed + 1
        SAY "[PASS]" test_name
    END
    ELSE DO
        failed = failed + 1
        SAY "[FAIL]" test_name
        SAY "       Expected RC:" expected_rc ", Got RC:" actual_rc
        IF result_contains ~= "" THEN
            SAY "       Expected substring: '" || result_contains || "'"
        SAY "       Actual Result: '" || STRIP(actual_result) || "'"
    END
RETURN

/* ------------------------------------------------------------
 * 1. STATUS command test
 * ------------------------------------------------------------ */
'STATUS'
CALL call_test "STATUS query returns active broker information", 0, "TinyDev-LSP Broker"

/* ------------------------------------------------------------
 * 2. PARSE command tests on project source files
 * ------------------------------------------------------------ */
'PARSE src/syntax.c'
CALL call_test "PARSE src/syntax.c caches symbols successfully", 0, "OK"

'PARSE include/syntax.h'
CALL call_test "PARSE include/syntax.h caches header symbols", 0, "OK"

'PARSE src/main.c'
CALL call_test "PARSE src/main.c succeeds", 0, "OK"

/* ------------------------------------------------------------
 * 3. SYMBOLS query tests
 * ------------------------------------------------------------ */
'SYMBOLS src/syntax.c'
CALL call_test "SYMBOLS src/syntax.c returns syntax_language_for_path", 0, "syntax_language_for_path"

'SYMBOLS include/syntax.h'
CALL call_test "SYMBOLS include/syntax.h contains SyntaxLanguage enum/type", 0, "SyntaxLanguage"

'SYMBOLS src/main.c'
CALL call_test "SYMBOLS src/main.c contains app_open_libraries", 0, "app_open_libraries"

/* ------------------------------------------------------------
 * 4. DIAGNOSTICS query tests
 * ------------------------------------------------------------ */
'DIAGNOSTICS src/syntax.c'
CALL call_test "DIAGNOSTICS on valid file src/syntax.c returns no errors", 0, ""

'DIAGNOSTICS include/syntax.h'
CALL call_test "DIAGNOSTICS on valid header include/syntax.h returns no errors", 0, ""

/* ------------------------------------------------------------
 * 5. DEFINITION lookup tests
 * ------------------------------------------------------------ */
/* In include/syntax.h, line 55 is syntax_scan_line */
'DEFINITION include/syntax.h 55 1 syntax_scan_line'
CALL call_test "DEFINITION finds syntax_scan_line in include/syntax.h", 0, "syntax.h"

/* In src/syntax.c, definition of syntax_language_for_path */
'DEFINITION src/syntax.c 39 16 syntax_language_for_path'
CALL call_test "DEFINITION finds syntax_language_for_path in src/syntax.c", 0, "src/syntax.c"

/* ------------------------------------------------------------
 * 6. COMPLETE autocompletion tests
 * ------------------------------------------------------------ */
/* In src/syntax.c, test keyword completion with prefix 'sw' */
'COMPLETE src/syntax.c 10 1 sw'
CALL call_test "COMPLETE with prefix 'sw' suggests 'switch'", 0, "switch"

/* In src/syntax.c, test symbol completion with prefix 'syntax_' */
'COMPLETE src/syntax.c 50 1 syntax_'
CALL call_test "COMPLETE with prefix 'syntax_' suggests syntax symbols", 0, "syntax_"

/* In include/editor.h, test struct/typedef completion with prefix 'Doc' */
'COMPLETE include/editor.h 60 1 Doc'
CALL call_test "COMPLETE with prefix 'Doc' suggests Document", 0, "Document"

/* ------------------------------------------------------------
 * 7. Error handling tests
 * ------------------------------------------------------------ */
'NONEXISTENT_COMMAND'
CALL call_test "Unknown command returns error code", 10, "Unknown command"

'SYMBOLS'
CALL call_test "SYMBOLS without arguments returns usage error", 10, "Usage"

'DIAGNOSTICS'
CALL call_test "DIAGNOSTICS without arguments returns usage error", 10, "Usage"

/* ------------------------------------------------------------
 * Summary
 * ------------------------------------------------------------ */
SAY "============================================================"
SAY " Test Summary: " passed || "/" || total " passed (" || failed " failed)"
SAY "============================================================"

IF failed > 0 THEN EXIT 5
EXIT 0
