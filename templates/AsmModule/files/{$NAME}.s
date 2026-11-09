*
* {$NAME} - {$PURPOSE}
* 
* Project: {$PROJECT}
* Copyright (c) {$YEAR} {$AUTHOR}
* {$AUTHORMAIL}
* Created {$DATE}
*

        XDEF    _{$NAME.ident}

        section code,code

* The exported routine. Out: d0 = 0.
_{$NAME.ident}:
        moveq   #0,d0
        rts
