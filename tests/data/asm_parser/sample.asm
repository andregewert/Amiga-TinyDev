* Sample for the assembler parser
; both comment forms

	include "exec/types.i"
	xdef	_Start

Version	equ	37
Limit	=	$100
Count	set	0
Count	set	Count+1
Regs	reg	d2-d7/a2-a6
Ptr	equr	a3

	rsreset
item_Next	rs.l	1
item_Size	rs.b	0

PUSH	macro
	movem.l	\1,-(sp)
lab\@:	dc.b	"\1",0
	endm

	section	code,code

_Start:
	PUSH	Regs
	moveq	#Version,d0
.loop	subq.l	#1,d0
	bne.s	.loop		count down
	move.l	item_Next(a3),d1
	rts			trailing comment (with

Other	cmp.b	#';',d0		; a quoted semicolon
1$:	bra.w	1$
	dc.b	'it''s',0
	even

	ifd	Version
	nop
	else
	nop
	endc

	end
this is not read ((
