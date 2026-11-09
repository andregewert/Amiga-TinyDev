*
* {$NAME}.s - {$PURPOSE}.
*
* Copyright (c) {$YEAR} {$AUTHOR}
* {$AUTHORMAIL}
* Created {$DATE}
*
* Started from the Workbench, a program must fetch the startup message
* before anything else and reply to it last, and it has no output stream.
*

        include "exec/types.i"
        include "dos/dos.i"
        include "dos/dosextens.i"
        include "lvo/exec_lib.i"
        include "lvo/dos_lib.i"

DOS_VERSION     equ     37

        section code,code

* The entry point: deals with the Workbench's message around main.
start:
        movem.l d2-d7/a2-a6,-(sp)
        moveq   #0,d7                   ; the Workbench's message, if any
        movea.l 4.w,a6
        suba.l  a1,a1
        jsr     _LVOFindTask(a6)
        movea.l d0,a4
        tst.l   pr_CLI(a4)
        bne.s   .run
        lea     pr_MsgPort(a4),a0
        jsr     _LVOWaitPort(a6)
        lea     pr_MsgPort(a4),a0
        jsr     _LVOGetMsg(a6)
        move.l  d0,d7
.run:
        bsr.s   main
        move.l  d0,d6                   ; the return code
        tst.l   d7
        beq.s   .exit
        movea.l 4.w,a6
        jsr     _LVOForbid(a6)          ; no Permit: the task ends first
        movea.l d7,a1
        jsr     _LVOReplyMsg(a6)
.exit:
        move.l  d6,d0
        movem.l (sp)+,d2-d7/a2-a6
        rts

* The program. In: d7 = the Workbench's message or 0, a6 = exec.library.
* Out: d0 = the return code.
main:
        moveq   #RETURN_FAIL,d5
        lea     dosname(pc),a1
        moveq   #DOS_VERSION,d0
        jsr     _LVOOpenLibrary(a6)
        tst.l   d0
        beq.s   .nodos
        movea.l d0,a5                   ; a5 = dos.library
        movea.l a5,a6
        bsr.s   greet
        move.l  d0,d5
        movea.l 4.w,a6
        movea.l a5,a1
        jsr     _LVOCloseLibrary(a6)
.nodos:
        move.l  d5,d0
        rts

* Print the text. In: d7 = the Workbench's message or 0, a6 = dos.library.
* Out: d0 = the return code.
greet:
        tst.l   d7
        bne.s   .console
        jsr     _LVOOutput(a6)
        move.l  d0,d1
        beq.s   .failed
        bsr.s   write_text
        rts
.console:
        lea     console(pc),a0
        move.l  a0,d1
        move.l  #MODE_NEWFILE,d2
        jsr     _LVOOpen(a6)
        move.l  d0,d4
        beq.s   .failed
        move.l  d4,d1
        bsr.s   write_text
        move.l  d0,d3                   ; Close() leaves d3 alone
        move.l  d4,d1
        jsr     _LVOClose(a6)
        move.l  d3,d0
        rts
.failed:
        moveq   #RETURN_FAIL,d0
        rts

* Write the text to a file. In: d1 = the file, a6 = dos.library.
* Out: d0 = the return code.
write_text:
        lea     text(pc),a0
        move.l  a0,d2
        moveq   #text_end-text,d3
        jsr     _LVOWrite(a6)
        cmp.l   d3,d0
        bne.s   .failed
        moveq   #RETURN_OK,d0
        rts
.failed:
        moveq   #RETURN_ERROR,d0
        rts

dosname:
        dc.b    "dos.library",0
console:
        dc.b    "CON:0/20/400/80/{$PROJECT}/WAIT/CLOSE",0
text:
        dc.b    "Hello, World!",10
text_end:
        even

        dc.b    "$VER: {$NAME} 1.0 ({$DATE.amiga})",0
