/*	$NetBSD: cpufunc.h,v 1.29 2003/09/06 09:08:35 rearnsha Exp $	*/

/*-
 * Copyright (c) 1997 Mark Brinicombe.
 * Copyright (c) 1997 Causality Limited
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 * 3. All advertising materials mentioning features or use of this software
 *    must display the following acknowledgement:
 *	This product includes software developed by Causality Limited.
 * 4. The name of Causality Limited may not be used to endorse or promote
 *    products derived from this software without specific prior written
 *    permission.
 *
 * THIS SOFTWARE IS PROVIDED BY CAUSALITY LIMITED ``AS IS'' AND ANY EXPRESS
 * OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
 * WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
 * DISCLAIMED. IN NO EVENT SHALL CAUSALITY LIMITED BE LIABLE FOR ANY DIRECT,
 * INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * RiscBSD kernel project
 *
 * cpufunc.h
 *
 * Prototypes for cpu, mmu and tlb related functions.
 *
 * $FreeBSD$
 */

#ifndef _MACHINE_CPUFUNC_H_
#define _MACHINE_CPUFUNC_H_

#ifdef _KERNEL

#include <sys/types.h>
#include <machine/cpuconf.h>
#include <machine/katelib.h> /* For in[bwl] and out[bwl] */

static __inline void
breakpoint(void)
{
	__asm(".word      0xe7ffffff");
}

struct cpu_functions {

	/* CPU functions */
	
	u_int	(*cf_id)		(void);
	void	(*cf_cpwait)		(void);

	/* MMU functions */

	u_int	(*cf_control)		(u_int bic, u_int eor);
	void	(*cf_domains)		(u_int domains);
	void	(*cf_setttb)		(u_int ttb);
	u_int	(*cf_faultstatus)	(void);
	u_int	(*cf_faultaddress)	(void);

	/* TLB functions */

	void	(*cf_tlb_flushID)	(void);	
	void	(*cf_tlb_flushID_SE)	(u_int va);	
	void	(*cf_tlb_flushI)	(void);
	void	(*cf_tlb_flushI_SE)	(u_int va);	
	void	(*cf_tlb_flushD)	(void);
	void	(*cf_tlb_flushD_SE)	(u_int va);	

	/*
	 * Cache operations:
	 *
	 * We define the following primitives:
	 *
	 *	icache_sync_all		Synchronize I-cache
	 *	icache_sync_range	Synchronize I-cache range
	 *
	 *	dcache_wbinv_all	Write-back and Invalidate D-cache
	 *	dcache_wbinv_range	Write-back and Invalidate D-cache range
	 *	dcache_inv_range	Invalidate D-cache range
	 *	dcache_wb_range		Write-back D-cache range
	 *
	 *	idcache_wbinv_all	Write-back and Invalidate D-cache,
	 *				Invalidate I-cache
	 *	idcache_wbinv_range	Write-back and Invalidate D-cache,
	 *				Invalidate I-cache range
	 *
	 * Note that the ARM term for "write-back" is "clean".  We use
	 * the term "write-back" since it's a more common way to describe
	 * the operation.
	 *
	 * There are some rules that must be followed:
	 *
	 *	ID-cache Invalidate All:
	 *		Unlike other functions, this one must never write back.
	 *		It is used to intialize the MMU when it is in an unknown
	 *		state (such as when it may have lines tagged as valid
	 *		that belong to a previous set of mappings).
	 *                                          
	 *	I-cache Synch (all or range):
	 *		The goal is to synchronize the instruction stream,
	 *		so you may beed to write-back dirty D-cache blocks
	 *		first.  If a range is requested, and you can't
	 *		synchronize just a range, you have to hit the whole
	 *		thing.
	 *
	 *	D-cache Write-Back and Invalidate range:
	 *		If you can't WB-Inv a range, you must WB-Inv the
	 *		entire D-cache.
	 *
	 *	D-cache Invalidate:
	 *		If you can't Inv the D-cache, you must Write-Back
	 *		and Invalidate.  Code that uses this operation
	 *		MUST NOT assume that the D-cache will not be written
	 *		back to memory.
	 *
	 *	D-cache Write-Back:
	 *		If you can't Write-back without doing an Inv,
	 *		that's fine.  Then treat this as a WB-Inv.
	 *		Skipping the invalidate is merely an optimization.
	 *
	 *	All operations:
	 *		Valid virtual addresses must be passed to each
	 *		cache operation.
	 */
	void	(*cf_icache_sync_all)	(void);
	void	(*cf_icache_sync_range)	(vm_offset_t, vm_size_t);

	void	(*cf_dcache_wbinv_all)	(void);
	void	(*cf_dcache_wbinv_range) (vm_offset_t, vm_size_t);
	void	(*cf_dcache_inv_range)	(vm_offset_t, vm_size_t);
	void	(*cf_dcache_wb_range)	(vm_offset_t, vm_size_t);

	void	(*cf_idcache_inv_all)	(void);
	void	(*cf_idcache_wbinv_all)	(void);
	void	(*cf_idcache_wbinv_range) (vm_offset_t, vm_size_t);
	void	(*cf_l2cache_wbinv_all) (void);
	void	(*cf_l2cache_wbinv_range) (vm_offset_t, vm_size_t);
	void	(*cf_l2cache_inv_range)	  (vm_offset_t, vm_size_t);
	void	(*cf_l2cache_wb_range)	  (vm_offset_t, vm_size_t);

	/* Other functions */

	void	(*cf_flush_prefetchbuf)	(void);
	void	(*cf_drain_writebuf)	(void);
	void	(*cf_flush_brnchtgt_C)	(void);
	void	(*cf_flush_brnchtgt_E)	(u_int va);

	void	(*cf_sleep)		(int mode);

	/* Soft functions */

	int	(*cf_dataabt_fixup)	(void *arg);
	int	(*cf_prefetchabt_fixup)	(void *arg);

	void	(*cf_context_switch)	(void);

	void	(*cf_setup)		(char *string);
};

extern struct cpu_functions cpufuncs;
extern u_int cputype;

#define cpu_id()		cpufuncs.cf_id()
#define	cpu_cpwait()		cpufuncs.cf_cpwait()

#define cpu_control(c, e)	cpufuncs.cf_control(c, e)
#define cpu_domains(d)		cpufuncs.cf_domains(d)
#define cpu_setttb(t)		cpufuncs.cf_setttb(t)
#define cpu_faultstatus()	cpufuncs.cf_faultstatus()
#define cpu_faultaddress()	cpufuncs.cf_faultaddress()

#ifndef SMP

#define	cpu_tlb_flushID()	cpufuncs.cf_tlb_flushID()
#define	cpu_tlb_flushID_SE(e)	cpufuncs.cf_tlb_flushID_SE(e)
#define	cpu_tlb_flushI()	cpufuncs.cf_tlb_flushI()
#define	cpu_tlb_flushI_SE(e)	cpufuncs.cf_tlb_flushI_SE(e)
#define	cpu_tlb_flushD()	cpufuncs.cf_tlb_flushD()
#define	cpu_tlb_flushD_SE(e)	cpufuncs.cf_tlb_flushD_SE(e)

#else
void tlb_broadcast(int);

#if defined(CPU_CORTEXA) || defined(CPU_MV_PJ4B) || defined(CPU_KRAIT)
#define TLB_BROADCAST	/* No need to explicitely send an IPI */
#else
#define TLB_BROADCAST	tlb_broadcast(7)
#endif

#define	cpu_tlb_flushID() do { \
	cpufuncs.cf_tlb_flushID(); \
	TLB_BROADCAST; \
} while(0)

#define	cpu_tlb_flushID_SE(e) do { \
	cpufuncs.cf_tlb_flushID_SE(e); \
	TLB_BROADCAST; \
} while(0)


#define	cpu_tlb_flushI() do { \
	cpufuncs.cf_tlb_flushI(); \
	TLB_BROADCAST; \
} while(0)


#define	cpu_tlb_flushI_SE(e) do { \
	cpufuncs.cf_tlb_flushI_SE(e); \
	TLB_BROADCAST; \
} while(0)


#define	cpu_tlb_flushD() do { \
	cpufuncs.cf_tlb_flushD(); \
	TLB_BROADCAST; \
} while(0)


#define	cpu_tlb_flushD_SE(e) do { \
	cpufuncs.cf_tlb_flushD_SE(e); \
	TLB_BROADCAST; \
} while(0)

#endif

#define	cpu_icache_sync_all()	cpufuncs.cf_icache_sync_all()
#define	cpu_icache_sync_range(a, s) cpufuncs.cf_icache_sync_range((a), (s))

#define	cpu_dcache_wbinv_all()	cpufuncs.cf_dcache_wbinv_all()
#define	cpu_dcache_wbinv_range(a, s) cpufuncs.cf_dcache_wbinv_range((a), (s))
#define	cpu_dcache_inv_range(a, s) cpufuncs.cf_dcache_inv_range((a), (s))
#define	cpu_dcache_wb_range(a, s) cpufuncs.cf_dcache_wb_range((a), (s))

#define	cpu_idcache_inv_all()	cpufuncs.cf_idcache_inv_all()
#define	cpu_idcache_wbinv_all()	cpufuncs.cf_idcache_wbinv_all()
#define	cpu_idcache_wbinv_range(a, s) cpufuncs.cf_idcache_wbinv_range((a), (s))
#define cpu_l2cache_wbinv_all()	cpufuncs.cf_l2cache_wbinv_all()
#define cpu_l2cache_wb_range(a, s) cpufuncs.cf_l2cache_wb_range((a), (s))
#define cpu_l2cache_inv_range(a, s) cpufuncs.cf_l2cache_inv_range((a), (s))
#define cpu_l2cache_wbinv_range(a, s) cpufuncs.cf_l2cache_wbinv_range((a), (s))

#define	cpu_flush_prefetchbuf()	cpufuncs.cf_flush_prefetchbuf()
#define	cpu_drain_writebuf()	cpufuncs.cf_drain_writebuf()
#define	cpu_flush_brnchtgt_C()	cpufuncs.cf_flush_brnchtgt_C()
#define	cpu_flush_brnchtgt_E(e)	cpufuncs.cf_flush_brnchtgt_E(e)

#define cpu_sleep(m)		cpufuncs.cf_sleep(m)

#define cpu_dataabt_fixup(a)		cpufuncs.cf_dataabt_fixup(a)
#define cpu_prefetchabt_fixup(a)	cpufuncs.cf_prefetchabt_fixup(a)
#define ABORT_FIXUP_OK		0	/* fixup succeeded */
#define ABORT_FIXUP_FAILED	1	/* fixup failed */
#define ABORT_FIXUP_RETURN	2	/* abort handler should return */

#define cpu_setup(a)			cpufuncs.cf_setup(a)

int	set_cpufuncs		(void);
#define ARCHITECTURE_NOT_PRESENT	1	/* known but not configured */
#define ARCHITECTURE_NOT_SUPPORTED	2	/* not known */

void	cpufunc_nullop		(void);
int	cpufunc_null_fixup	(void *);
int	early_abort_fixup	(void *);
int	late_abort_fixup	(void *);
u_int	cpufunc_id		(void);
u_int	cpufunc_cpuid		(void);
u_int	cpufunc_control		(u_int clear, u_int bic);
void	cpufunc_domains		(u_int domains);
u_int	cpufunc_faultstatus	(void);
u_int	cpufunc_faultaddress	(void);
u_int	cpu_pfr			(int);

#if defined(CPU_FA526) || defined(CPU_FA626TE)
void	fa526_setup		(char *arg);
void	fa526_setttb		(u_int ttb);
void	fa526_context_switch	(void);
void	fa526_cpu_sleep		(int);
void	fa526_tlb_flushI_SE	(u_int);
void	fa526_tlb_flushID_SE	(u_int);
void	fa526_flush_prefetchbuf	(void);
void	fa526_flush_brnchtgt_E	(u_int);

void	fa526_icache_sync_all	(void);
void	fa526_icache_sync_range(vm_offset_t start, vm_size_t end);
void	fa526_dcache_wbinv_all	(void);
void	fa526_dcache_wbinv_range(vm_offset_t start, vm_size_t end);
void	fa526_dcache_inv_range	(vm_offset_t start, vm_size_t end);
void	fa526_dcache_wb_range	(vm_offset_t start, vm_size_t end);
void	fa526_idcache_wbinv_all(void);
void	fa526_idcache_wbinv_range(vm_offset_t start, vm_size_t end);
#endif


#ifdef CPU_ARM9
void	arm9_setttb		(u_int);

void	arm9_tlb_flushID_SE	(u_int va);

void	arm9_icache_sync_all	(void);
void	arm9_icache_sync_range	(vm_offset_t, vm_size_t);

void	arm9_dcache_wbinv_all	(void);
void	arm9_dcache_wbinv_range (vm_offset_t, vm_size_t);
void	arm9_dcache_inv_range	(vm_offset_t, vm_size_t);
void	arm9_dcache_wb_range	(vm_offset_t, vm_size_t);

void	arm9_idcache_wbinv_all	(void);
void	arm9_idcache_wbinv_range (vm_offset_t, vm_size_t);

void	arm9_context_switch	(void);

void	arm9_setup		(char *string);

extern unsigned arm9_dcache_sets_max;
extern unsigned arm9_dcache_sets_inc;
extern unsigned arm9_dcache_index_max;
extern unsigned arm9_dcache_index_inc;
#endif

#if defined(CPU_ARM9E) || defined(CPU_ARM10)
void	arm10_setttb		(u_int);

void	arm10_tlb_flushID_SE	(u_int);
void	arm10_tlb_flushI_SE	(u_int);

void	arm10_icache_sync_all	(void);
void	arm10_icache_sync_range	(vm_offset_t, vm_size_t);

void	arm10_dcache_wbinv_all	(void);
void	arm10_dcache_wbinv_range (vm_offset_t, vm_size_t);
void	arm10_dcache_inv_range	(vm_offset_t, vm_size_t);
void	arm10_dcache_wb_range	(vm_offset_t, vm_size_t);

void	arm10_idcache_wbinv_all	(void);
void	arm10_idcache_wbinv_range (vm_offset_t, vm_size_t);

void	arm10_context_switch	(void);

void	arm10_setup		(char *string);

extern unsigned arm10_dcache_sets_max;
extern unsigned arm10_dcache_sets_inc;
extern unsigned arm10_dcache_index_max;
extern unsigned arm10_dcache_index_inc;

u_int	sheeva_control_ext 		(u_int, u_int);
void	sheeva_cpu_sleep		(int);
void	sheeva_setttb			(u_int);
void	sheeva_dcache_wbinv_range	(vm_offset_t, vm_size_t);
void	sheeva_dcache_inv_range		(vm_offset_t, vm_size_t);
void	sheeva_dcache_wb_range		(vm_offset_t, vm_size_t);
void	sheeva_idcache_wbinv_range	(vm_offset_t, vm_size_t);

void	sheeva_l2cache_wbinv_range	(vm_offset_t, vm_size_t);
void	sheeva_l2cache_inv_range	(vm_offset_t, vm_size_t);
void	sheeva_l2cache_wb_range		(vm_offset_t, vm_size_t);
void	sheeva_l2cache_wbinv_all	(void);
#endif

#if defined(CPU_ARM1136) || defined(CPU_ARM1176) || \
	defined(CPU_MV_PJ4B) || defined(CPU_CORTEXA) || defined(CPU_KRAIT)
void	arm11_setttb		(u_int);
void	arm11_sleep		(int);

void	arm11_tlb_flushID_SE	(u_int);
void	arm11_tlb_flushI_SE	(u_int);

void	arm11_context_switch	(void);

void	arm11_setup		(char *string);
void	arm11_tlb_flushID	(void);
void	arm11_tlb_flushI	(void);
void	arm11_tlb_flushD	(void);
void	arm11_tlb_flushD_SE	(u_int va);

void	arm11_drain_writebuf	(void);

void	pj4b_setttb			(u_int);

void	pj4b_drain_readbuf		(void);
void	pj4b_flush_brnchtgt_all		(void);
void	pj4b_flush_brnchtgt_va		(u_int);
void	pj4b_sleep			(int);

void	armv6_icache_sync_all		(void);
void	armv6_icache_sync_range		(vm_offset_t, vm_size_t);

void	armv6_dcache_wbinv_all		(void);
void	armv6_dcache_wbinv_range	(vm_offset_t, vm_size_t);
void	armv6_dcache_inv_range		(vm_offset_t, vm_size_t);
void	armv6_dcache_wb_range		(vm_offset_t, vm_size_t);

void	armv6_idcache_inv_all		(void);
void	armv6_idcache_wbinv_all		(void);
void	armv6_idcache_wbinv_range	(vm_offset_t, vm_size_t);

void	armv7_setttb			(u_int);
void	armv7_tlb_flushID		(void);
void	armv7_tlb_flushID_SE		(u_int);
void	armv7_icache_sync_all		();
void	armv7_icache_sync_range		(vm_offset_t, vm_size_t);
void	armv7_idcache_wbinv_range	(vm_offset_t, vm_size_t);
void	armv7_idcache_inv_all		(void);
void	armv7_dcache_wbinv_all		(void);
void	armv7_idcache_wbinv_all		(void);
void	armv7_dcache_wbinv_range	(vm_offset_t, vm_size_t);
void	armv7_dcache_inv_range		(vm_offset_t, vm_size_t);
void	armv7_dcache_wb_range		(vm_offset_t, vm_size_t);
void	armv7_cpu_sleep			(int);
void	armv7_setup			(char *string);
void	armv7_context_switch		(void);
void	armv7_drain_writebuf		(void);
void	armv7_sev			(void);
void	armv7_sleep			(int unused);
u_int	armv7_auxctrl			(u_int, u_int);
void	pj4bv7_setup			(char *string);
void	pj4b_config			(void);

int	get_core_id			(void);

void	armadaxp_idcache_wbinv_all	(void);

void 	cortexa_setup			(char *);
#endif

#if defined(CPU_ARM1136) || defined(CPU_ARM1176)
void    arm11x6_setttb                  (u_int);
void    arm11x6_idcache_wbinv_all       (void);
void    arm11x6_dcache_wbinv_all        (void);
void    arm11x6_icache_sync_all         (void);
void    arm11x6_flush_prefetchbuf       (void);
void    arm11x6_icache_sync_range       (vm_offset_t, vm_size_t);
void    arm11x6_idcache_wbinv_range     (vm_offset_t, vm_size_t);
void    arm11x6_setup                   (char *string);
void    arm11x6_sleep                   (int);  /* no ref. for errata */
#endif
#if defined(CPU_ARM1136)
void    arm1136_sleep_rev0              (int);  /* for errata 336501 */
#endif

#if defined(CPU_ARM9E) || defined (CPU_ARM10)
void	armv5_ec_setttb(u_int);

void	armv5_ec_icache_sync_all(void);
void	armv5_ec_icache_sync_range(vm_offset_t, vm_size_t);

void	armv5_ec_dcache_wbinv_all(void);
void	armv5_ec_dcache_wbinv_range(vm_offset_t, vm_size_t);
void	armv5_ec_dcache_inv_range(vm_offset_t, vm_size_t);
void	armv5_ec_dcache_wb_range(vm_offset_t, vm_size_t);

void	armv5_ec_idcache_wbinv_all(void);
void	armv5_ec_idcache_wbinv_range(vm_offset_t, vm_size_t);
#endif

#if defined (CPU_ARM10)
void	armv5_setttb(u_int);

void	armv5_icache_sync_all(void);
void	armv5_icache_sync_range(vm_offset_t, vm_size_t);

void	armv5_dcache_wbinv_all(void);
void	armv5_dcache_wbinv_range(vm_offset_t, vm_size_t);
void	armv5_dcache_inv_range(vm_offset_t, vm_size_t);
void	armv5_dcache_wb_range(vm_offset_t, vm_size_t);

void	armv5_idcache_wbinv_all(void);
void	armv5_idcache_wbinv_range(vm_offset_t, vm_size_t);

extern unsigned armv5_dcache_sets_max;
extern unsigned armv5_dcache_sets_inc;
extern unsigned armv5_dcache_index_max;
extern unsigned armv5_dcache_index_inc;
#endif

#if defined(CPU_ARM9) || defined(CPU_ARM9E) || defined(CPU_ARM10) ||	\
  defined(CPU_XSCALE_80200) || defined(CPU_XSCALE_80321) ||		\
  defined(CPU_FA526) || defined(CPU_FA626TE) ||				\
  defined(CPU_XSCALE_PXA2X0) || defined(CPU_XSCALE_IXP425) ||		\
  defined(CPU_XSCALE_80219) || defined(CPU_XSCALE_81342)

void	armv4_tlb_flushID	(void);
void	armv4_tlb_flushI	(void);
void	armv4_tlb_flushD	(void);
void	armv4_tlb_flushD_SE	(u_int va);

void	armv4_drain_writebuf	(void);
void	armv4_idcache_inv_all	(void);
#endif

#if defined(CPU_XSCALE_80200) || defined(CPU_XSCALE_80321) ||	\
  defined(CPU_XSCALE_PXA2X0) || defined(CPU_XSCALE_IXP425) ||	\
  defined(CPU_XSCALE_80219) || defined(CPU_XSCALE_81342)
void	xscale_cpwait		(void);

void	xscale_cpu_sleep	(int mode);

u_int	xscale_control		(u_int clear, u_int bic);

void	xscale_setttb		(u_int ttb);

void	xscale_tlb_flushID_SE	(u_int va);

void	xscale_cache_flushID	(void);
void	xscale_cache_flushI	(void);
void	xscale_cache_flushD	(void);
void	xscale_cache_flushD_SE	(u_int entry);

void	xscale_cache_cleanID	(void);
void	xscale_cache_cleanD	(void);
void	xscale_cache_cleanD_E	(u_int entry);

void	xscale_cache_clean_minidata (void);

void	xscale_cache_purgeID	(void);
void	xscale_cache_purgeID_E	(u_int entry);
void	xscale_cache_purgeD	(void);
void	xscale_cache_purgeD_E	(u_int entry);

void	xscale_cache_syncI	(void);
void	xscale_cache_cleanID_rng (vm_offset_t start, vm_size_t end);
void	xscale_cache_cleanD_rng	(vm_offset_t start, vm_size_t end);
void	xscale_cache_purgeID_rng (vm_offset_t start, vm_size_t end);
void	xscale_cache_purgeD_rng	(vm_offset_t start, vm_size_t end);
void	xscale_cache_syncI_rng	(vm_offset_t start, vm_size_t end);
void	xscale_cache_flushD_rng	(vm_offset_t start, vm_size_t end);

void	xscale_context_switch	(void);

void	xscale_setup		(char *string);
#endif	/* CPU_XSCALE_80200 || CPU_XSCALE_80321 || CPU_XSCALE_PXA2X0 || CPU_XSCALE_IXP425
	   CPU_XSCALE_80219 */

#ifdef	CPU_XSCALE_81342

void	xscalec3_l2cache_purge	(void);
void	xscalec3_cache_purgeID	(void);
void	xscalec3_cache_purgeD	(void);
void	xscalec3_cache_cleanID	(void);
void	xscalec3_cache_cleanD	(void);
void	xscalec3_cache_syncI	(void);

void	xscalec3_cache_purgeID_rng 	(vm_offset_t start, vm_size_t end);
void	xscalec3_cache_purgeD_rng	(vm_offset_t start, vm_size_t end);
void	xscalec3_cache_cleanID_rng	(vm_offset_t start, vm_size_t end);
void	xscalec3_cache_cleanD_rng	(vm_offset_t start, vm_size_t end);
void	xscalec3_cache_syncI_rng	(vm_offset_t start, vm_size_t end);

void	xscalec3_l2cache_flush_rng	(vm_offset_t, vm_size_t);
void	xscalec3_l2cache_clean_rng	(vm_offset_t start, vm_size_t end);
void	xscalec3_l2cache_purge_rng	(vm_offset_t start, vm_size_t end);


void	xscalec3_setttb		(u_int ttb);
void	xscalec3_context_switch	(void);

#endif /* CPU_XSCALE_81342 */

#define tlb_flush	cpu_tlb_flushID
#define setttb		cpu_setttb
#define drain_writebuf	cpu_drain_writebuf

/*
 * Macros for manipulating CPU interrupts
 */
static __inline u_int32_t __set_cpsr_c(u_int bic, u_int eor) __attribute__((__unused__));

static __inline u_int32_t
__set_cpsr_c(u_int bic, u_int eor)
{
	u_int32_t	tmp, ret;

	__asm __volatile(
		"mrs     %0, cpsr\n"	/* Get the CPSR */
		"bic	 %1, %0, %2\n"	/* Clear bits */
		"eor	 %1, %1, %3\n"	/* XOR bits */
		"msr     cpsr_c, %1\n"	/* Set the control field of CPSR */
	: "=&r" (ret), "=&r" (tmp)
	: "r" (bic), "r" (eor) : "memory");

	return ret;
}

#define	ARM_CPSR_F32	(1 << 6)	/* FIQ disable */
#define	ARM_CPSR_I32	(1 << 7)	/* IRQ disable */

#define disable_interrupts(mask)					\
	(__set_cpsr_c((mask) & (ARM_CPSR_I32 | ARM_CPSR_F32),		\
		      (mask) & (ARM_CPSR_I32 | ARM_CPSR_F32)))

#define enable_interrupts(mask)						\
	(__set_cpsr_c((mask) & (ARM_CPSR_I32 | ARM_CPSR_F32), 0))

#define restore_interrupts(old_cpsr)					\
	(__set_cpsr_c((ARM_CPSR_I32 | ARM_CPSR_F32),			\
		      (old_cpsr) & (ARM_CPSR_I32 | ARM_CPSR_F32)))

static __inline register_t
intr_disable(void)
{
	register_t s;

	s = disable_interrupts(ARM_CPSR_I32 | ARM_CPSR_F32);
	return (s);
}

static __inline void
intr_restore(register_t s)
{

	restore_interrupts(s);
}

/* Functions to manipulate the CPSR. */
u_int	SetCPSR(u_int bic, u_int eor);
u_int	GetCPSR(void);

/*
 * Functions to manipulate cpu r13
 * (in arm/arm32/setstack.S)
 */

void set_stackptr	(u_int mode, u_int address);
u_int get_stackptr	(u_int mode);

/*
 * Miscellany
 */

int get_pc_str_offset	(void);

/*
 * CPU functions from locore.S
 */

void cpu_reset		(void) __attribute__((__noreturn__));

/*
 * Cache info variables.
 */

/* PRIMARY CACHE VARIABLES */
extern int	arm_picache_size;
extern int	arm_picache_line_size;
extern int	arm_picache_ways;

extern int	arm_pdcache_size;	/* and unified */
extern int	arm_pdcache_line_size;
extern int	arm_pdcache_ways;

extern int	arm_pcache_type;
extern int	arm_pcache_unified;

extern int	arm_dcache_align;
extern int	arm_dcache_align_mask;

extern u_int	arm_cache_level;
extern u_int	arm_cache_loc;
extern u_int	arm_cache_type[14];

#endif	/* _KERNEL */
#endif	/* _MACHINE_CPUFUNC_H_ */

/* End of cpufunc.h */
