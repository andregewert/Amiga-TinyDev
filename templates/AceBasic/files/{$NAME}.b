REM {$NAME}.b - {$PURPOSE}.
REM
REM Copyright (c) {$YEAR} {$AUTHOR}
REM {$AUTHORMAIL}
REM Created {$DATE}

SUB ShowGreeting(STRING who)
  PRINT "Hello, ";who;"!"
END SUB

WINDOW 1,"{$PROJECT}",(0,0)-(400,100)

ShowGreeting("World")
PRINT
PRINT "Press a key to quit."

WHILE INKEY$="" : SLEEP : WEND

WINDOW CLOSE 1
END
