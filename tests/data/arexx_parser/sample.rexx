/* Sample of the ARexx parser: a program without mistakes (Grüße) */
OPTIONS RESULTS
SIGNAL ON SYNTAX
PARSE ARG file count
IF file = '' THEN DO
    SAY 'no file'
    EXIT 10
END
ELSE SAY 'file:' file

total = Add(count, 1); names.0 = 0
DO i = 1 TO total BY 1
    names.i = LEFT(file, i, ' ')
    IF i > 5 THEN LEAVE
END i
/* a comment /* inside */ a comment */
SELECT
    WHEN total = 1 THEN SAY 'one'
    WHEN total = 2 THEN DO
        SAY 'two'
    END
    OTHERWISE
        SAY 'many'
END
IF total > 1 THEN IF total > 2 THEN SAY 'a'; ELSE SAY 'b'; ELSE SAY 'c'
long = 'a' ||,
       'b'
mask = 'FF 00'x; bits = '0101'b; quote = 'it''s'
PARSE VALUE Add(1, 2) WITH first ',' second .
ADDRESS COMMAND 'List'
CALL Report total
EXIT 0

Add: PROCEDURE EXPOSE sum
    ARG a, b
    sum = a + b
    local = sum
    RETURN sum

Report:
    ARG n
    SAY n
    RETURN

SYNTAX:
    SAY 'error' RC 'in line' SIGL
    EXIT 20
