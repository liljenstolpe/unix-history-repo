/*
 * Copyright (c) 1992, 1993
 *	Regents of the University of California.  All rights reserved.
 *
 * This code is derived from software contributed to Berkeley by
 * Ralph Campbell.
 *
 * %sccs.include.redist.c%
 *
 *	@(#)start.s	8.1 (Berkeley) %G%
 */

/*
 * start.s -
 *
 *     Contains code that is the first executed at boot time.
 *
 *	Copyright (C) 1989 Digital Equipment Corporation.
 *	Permission to use, copy, modify, and distribute this software and
 *	its documentation for any purpose and without fee is hereby granted,
 *	provided that the above copyright notice appears in all copies.  
 *	Digital Equipment Corporation makes no representations about the
 *	suitability of this software for any purpose.  It is provided "as is"
 *	without express or implied warranty.
 *
 * from: $Header: /sprite/src/boot/decprom/ds3100.md/RCS/start.s,
 *	v 1.1 90/02/16 16:19:39 shirriff Exp $ SPRITE (DECWRL)
 */

#include <machine/regdef.h>
#include <machine/machConst.h>
#include <machine/machAsmDefs.h>
#include <pmax/stand/dec_prom.h>

/*
 * Amount to take off of the stack for the benefit of the debugger.
 */
#define START_FRAME	((4 * 4) + 4 + 4)

	.globl	start
start:
	.set	noreorder
	la	sp, start - START_FRAME
	sw	zero, START_FRAME - 4(sp)	# Zero out old ra for debugger
	sw	zero, START_FRAME - 8(sp)	# Zero out old fp for debugger
	move	s0, a0				# save argc
	move	s1, a1				# save argv
	beq	a3, 0x30464354, 1f		# jump if boot from DS5000
	move	s3, a3				# save call vector
	la	s3, callvec			# init call vector
1:
	la	a0, edata			# clear BSS
	la	a1, end
	jal	bzero				# bzero(edata, end - edata)
	subu	a1, a1, a0
	sw	s3, callv			# save call vector
	move	a0, s0				# restore argc
	jal	main				# main(argc, argv)
	move	a1, s1				# restore argv
	li	v0, DEC_PROM_RESTART		# done, so call prom
	j	v0
	nop

/* dummy routine for gcc2 */
	.globl	__main
__main:
	j	ra
	nop

LEAF(prom_restart)
	li	v0, DEC_PROM_RESTART
	j	v0
	nop
END(prom_restart)

LEAF(prom_open)
	li	v0, DEC_PROM_OPEN
	j	v0
	nop
END(prom_open)

LEAF(prom_lseek)
	li	v0, DEC_PROM_LSEEK
	j	v0
	nop
END(prom_lseek)

LEAF(prom_read)
	li	v0, DEC_PROM_READ
	j	v0
	nop
END(prom_read)

LEAF(printf)
	lw	v0, callv	# get pointer to call back vectors
	sw	a1, 4(sp)	# store args on stack for printf
	lw	v0, 48(v0)	# offset for callv->printf
	sw	a2, 8(sp)
	j	v0		# call PROM printf
	sw	a3, 12(sp)
END(printf)
