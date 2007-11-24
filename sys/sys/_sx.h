/*-
 * Copyright (c) 2007 Attilio Rao <attilio@freebsd.org>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice(s), this list of conditions and the following disclaimer as
 *    the first lines of this file unmodified other than the possible 
 *    addition of one or more copyright notices.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice(s), this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDER(S) ``AS IS'' AND ANY
 * EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED.  IN NO EVENT SHALL THE COPYRIGHT HOLDER(S) BE LIABLE FOR ANY
 * DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH
 * DAMAGE.
 *
 * $FreeBSD$
 */

#ifndef	_SYS__SX_H_
#define	_SYS__SX_H_

#include <sys/condvar.h>

/*
 * Shared/exclusive lock main structure definition.
 *
 * Note, to preserve compatibility we have extra fields from
 * the previous implementation left over.
 */
struct sx {
	struct lock_object	lock_object;
	/* was: struct mtx *sx_lock; */
	volatile uintptr_t	sx_lock;
	/* was: int sx_cnt; */
	volatile unsigned	sx_recurse;
	/*
	 * The following fields are unused but kept to preserve
	 * sizeof(struct sx) for 6.x compat.
	 */
	struct cv       sx_shrd_cv;	/* unused */
	int             sx_shrd_wcnt;	/* unused */
	struct cv       sx_excl_cv;	/* unused */
	int             sx_excl_wcnt;	/* unused */
	struct thread   *sx_xholder;	/* unused */
};

#endif	/* !_SYS__SX_H_ */
