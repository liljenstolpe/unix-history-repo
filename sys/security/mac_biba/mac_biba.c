/*-
 * Copyright (c) 1999-2002, 2007 Robert N. M. Watson
 * Copyright (c) 2001-2005 McAfee, Inc.
 * Copyright (c) 2006 SPARTA, Inc.
 * All rights reserved.
 *
 * This software was developed by Robert Watson for the TrustedBSD Project.
 *
 * This software was developed for the FreeBSD Project in part by McAfee
 * Research, the Security Research Division of McAfee, Inc. under
 * DARPA/SPAWAR contract N66001-01-C-8035 ("CBOSS"), as part of the DARPA
 * CHATS research program.
 *
 * This software was enhanced by SPARTA ISSO under SPAWAR contract
 * N66001-04-C-6019 ("SEFOS").
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR AND CONTRIBUTORS ``AS IS'' AND
 * ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED.  IN NO EVENT SHALL THE AUTHOR OR CONTRIBUTORS BE LIABLE
 * FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS
 * OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT
 * LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY
 * OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF
 * SUCH DAMAGE.
 *
 * $FreeBSD$
 */

/*
 * Developed by the TrustedBSD Project.
 *
 * Biba fixed label mandatory integrity policy.
 */

#include <sys/param.h>
#include <sys/conf.h>
#include <sys/extattr.h>
#include <sys/kernel.h>
#include <sys/ksem.h>
#include <sys/malloc.h>
#include <sys/mman.h>
#include <sys/mount.h>
#include <sys/priv.h>
#include <sys/proc.h>
#include <sys/sbuf.h>
#include <sys/systm.h>
#include <sys/sysproto.h>
#include <sys/sysent.h>
#include <sys/systm.h>
#include <sys/vnode.h>
#include <sys/file.h>
#include <sys/socket.h>
#include <sys/socketvar.h>
#include <sys/pipe.h>
#include <sys/sx.h>
#include <sys/sysctl.h>
#include <sys/msg.h>
#include <sys/sem.h>
#include <sys/shm.h>

#include <fs/devfs/devfs.h>

#include <net/bpfdesc.h>
#include <net/if.h>
#include <net/if_types.h>
#include <net/if_var.h>

#include <netinet/in.h>
#include <netinet/in_pcb.h>
#include <netinet/ip_var.h>

#include <vm/uma.h>
#include <vm/vm.h>

#include <security/mac/mac_policy.h>
#include <security/mac_biba/mac_biba.h>

SYSCTL_DECL(_security_mac);

SYSCTL_NODE(_security_mac, OID_AUTO, biba, CTLFLAG_RW, 0,
    "TrustedBSD mac_biba policy controls");

static int	mac_biba_label_size = sizeof(struct mac_biba);
SYSCTL_INT(_security_mac_biba, OID_AUTO, label_size, CTLFLAG_RD,
    &mac_biba_label_size, 0, "Size of struct mac_biba");

static int	mac_biba_enabled = 1;
SYSCTL_INT(_security_mac_biba, OID_AUTO, enabled, CTLFLAG_RW,
    &mac_biba_enabled, 0, "Enforce MAC/Biba policy");
TUNABLE_INT("security.mac.biba.enabled", &mac_biba_enabled);

static int	destroyed_not_inited;
SYSCTL_INT(_security_mac_biba, OID_AUTO, destroyed_not_inited, CTLFLAG_RD,
    &destroyed_not_inited, 0, "Count of labels destroyed but not inited");

static int	trust_all_interfaces = 0;
SYSCTL_INT(_security_mac_biba, OID_AUTO, trust_all_interfaces, CTLFLAG_RD,
    &trust_all_interfaces, 0, "Consider all interfaces 'trusted' by MAC/Biba");
TUNABLE_INT("security.mac.biba.trust_all_interfaces", &trust_all_interfaces);

static char	trusted_interfaces[128];
SYSCTL_STRING(_security_mac_biba, OID_AUTO, trusted_interfaces, CTLFLAG_RD,
    trusted_interfaces, 0, "Interfaces considered 'trusted' by MAC/Biba");
TUNABLE_STR("security.mac.biba.trusted_interfaces", trusted_interfaces,
    sizeof(trusted_interfaces));

static int	max_compartments = MAC_BIBA_MAX_COMPARTMENTS;
SYSCTL_INT(_security_mac_biba, OID_AUTO, max_compartments, CTLFLAG_RD,
    &max_compartments, 0, "Maximum supported compartments");

static int	ptys_equal = 0;
SYSCTL_INT(_security_mac_biba, OID_AUTO, ptys_equal, CTLFLAG_RW,
    &ptys_equal, 0, "Label pty devices as biba/equal on create");
TUNABLE_INT("security.mac.biba.ptys_equal", &ptys_equal);

static int	interfaces_equal;
SYSCTL_INT(_security_mac_biba, OID_AUTO, interfaces_equal, CTLFLAG_RW,
    &interfaces_equal, 0, "Label network interfaces as biba/equal on create");
TUNABLE_INT("security.mac.biba.interfaces_equal", &interfaces_equal);

static int	revocation_enabled = 0;
SYSCTL_INT(_security_mac_biba, OID_AUTO, revocation_enabled, CTLFLAG_RW,
    &revocation_enabled, 0, "Revoke access to objects on relabel");
TUNABLE_INT("security.mac.biba.revocation_enabled", &revocation_enabled);

static int	mac_biba_slot;
#define	SLOT(l)	((struct mac_biba *)mac_label_get((l), mac_biba_slot))
#define	SLOT_SET(l, val) mac_label_set((l), mac_biba_slot, (uintptr_t)(val))

static uma_zone_t	zone_biba;

static __inline int
biba_bit_set_empty(u_char *set) {
	int i;

	for (i = 0; i < MAC_BIBA_MAX_COMPARTMENTS >> 3; i++)
		if (set[i] != 0)
			return (0);
	return (1);
}

static struct mac_biba *
biba_alloc(int flag)
{

	return (uma_zalloc(zone_biba, flag | M_ZERO));
}

static void
biba_free(struct mac_biba *mac_biba)
{

	if (mac_biba != NULL)
		uma_zfree(zone_biba, mac_biba);
	else
		atomic_add_int(&destroyed_not_inited, 1);
}

static int
biba_atmostflags(struct mac_biba *mac_biba, int flags)
{

	if ((mac_biba->mb_flags & flags) != mac_biba->mb_flags)
		return (EINVAL);
	return (0);
}

static int
mac_biba_dominate_element(struct mac_biba_element *a,
    struct mac_biba_element *b)
{
	int bit;

	switch (a->mbe_type) {
	case MAC_BIBA_TYPE_EQUAL:
	case MAC_BIBA_TYPE_HIGH:
		return (1);

	case MAC_BIBA_TYPE_LOW:
		switch (b->mbe_type) {
		case MAC_BIBA_TYPE_GRADE:
		case MAC_BIBA_TYPE_HIGH:
			return (0);

		case MAC_BIBA_TYPE_EQUAL:
		case MAC_BIBA_TYPE_LOW:
			return (1);

		default:
			panic("mac_biba_dominate_element: b->mbe_type invalid");
		}

	case MAC_BIBA_TYPE_GRADE:
		switch (b->mbe_type) {
		case MAC_BIBA_TYPE_EQUAL:
		case MAC_BIBA_TYPE_LOW:
			return (1);

		case MAC_BIBA_TYPE_HIGH:
			return (0);

		case MAC_BIBA_TYPE_GRADE:
			for (bit = 1; bit <= MAC_BIBA_MAX_COMPARTMENTS; bit++)
				if (!MAC_BIBA_BIT_TEST(bit,
				    a->mbe_compartments) &&
				    MAC_BIBA_BIT_TEST(bit, b->mbe_compartments))
					return (0);
			return (a->mbe_grade >= b->mbe_grade);

		default:
			panic("mac_biba_dominate_element: b->mbe_type invalid");
		}

	default:
		panic("mac_biba_dominate_element: a->mbe_type invalid");
	}

	return (0);
}

static int
mac_biba_subject_dominate_high(struct mac_biba *mac_biba)
{
	struct mac_biba_element *element;

	KASSERT((mac_biba->mb_flags & MAC_BIBA_FLAG_EFFECTIVE) != 0,
	    ("mac_biba_effective_in_range: mac_biba not effective"));
	element = &mac_biba->mb_effective;

	return (element->mbe_type == MAC_BIBA_TYPE_EQUAL ||
	    element->mbe_type == MAC_BIBA_TYPE_HIGH);
}

static int
mac_biba_range_in_range(struct mac_biba *rangea, struct mac_biba *rangeb)
{

	return (mac_biba_dominate_element(&rangeb->mb_rangehigh,
	    &rangea->mb_rangehigh) &&
	    mac_biba_dominate_element(&rangea->mb_rangelow,
	    &rangeb->mb_rangelow));
}

static int
mac_biba_effective_in_range(struct mac_biba *effective,
    struct mac_biba *range)
{

	KASSERT((effective->mb_flags & MAC_BIBA_FLAG_EFFECTIVE) != 0,
	    ("mac_biba_effective_in_range: a not effective"));
	KASSERT((range->mb_flags & MAC_BIBA_FLAG_RANGE) != 0,
	    ("mac_biba_effective_in_range: b not range"));

	return (mac_biba_dominate_element(&range->mb_rangehigh,
	    &effective->mb_effective) &&
	    mac_biba_dominate_element(&effective->mb_effective,
	    &range->mb_rangelow));

	return (1);
}

static int
mac_biba_dominate_effective(struct mac_biba *a, struct mac_biba *b)
{
	KASSERT((a->mb_flags & MAC_BIBA_FLAG_EFFECTIVE) != 0,
	    ("mac_biba_dominate_effective: a not effective"));
	KASSERT((b->mb_flags & MAC_BIBA_FLAG_EFFECTIVE) != 0,
	    ("mac_biba_dominate_effective: b not effective"));

	return (mac_biba_dominate_element(&a->mb_effective, &b->mb_effective));
}

static int
mac_biba_equal_element(struct mac_biba_element *a, struct mac_biba_element *b)
{

	if (a->mbe_type == MAC_BIBA_TYPE_EQUAL ||
	    b->mbe_type == MAC_BIBA_TYPE_EQUAL)
		return (1);

	return (a->mbe_type == b->mbe_type && a->mbe_grade == b->mbe_grade);
}

static int
mac_biba_equal_effective(struct mac_biba *a, struct mac_biba *b)
{

	KASSERT((a->mb_flags & MAC_BIBA_FLAG_EFFECTIVE) != 0,
	    ("mac_biba_equal_effective: a not effective"));
	KASSERT((b->mb_flags & MAC_BIBA_FLAG_EFFECTIVE) != 0,
	    ("mac_biba_equal_effective: b not effective"));

	return (mac_biba_equal_element(&a->mb_effective, &b->mb_effective));
}

static int
mac_biba_contains_equal(struct mac_biba *mac_biba)
{

	if (mac_biba->mb_flags & MAC_BIBA_FLAG_EFFECTIVE)
		if (mac_biba->mb_effective.mbe_type == MAC_BIBA_TYPE_EQUAL)
			return (1);

	if (mac_biba->mb_flags & MAC_BIBA_FLAG_RANGE) {
		if (mac_biba->mb_rangelow.mbe_type == MAC_BIBA_TYPE_EQUAL)
			return (1);
		if (mac_biba->mb_rangehigh.mbe_type == MAC_BIBA_TYPE_EQUAL)
			return (1);
	}

	return (0);
}

static int
mac_biba_subject_privileged(struct mac_biba *mac_biba)
{

	KASSERT((mac_biba->mb_flags & MAC_BIBA_FLAGS_BOTH) ==
	    MAC_BIBA_FLAGS_BOTH,
	    ("mac_biba_subject_privileged: subject doesn't have both labels"));

	/* If the effective is EQUAL, it's ok. */
	if (mac_biba->mb_effective.mbe_type == MAC_BIBA_TYPE_EQUAL)
		return (0);

	/* If either range endpoint is EQUAL, it's ok. */
	if (mac_biba->mb_rangelow.mbe_type == MAC_BIBA_TYPE_EQUAL ||
	    mac_biba->mb_rangehigh.mbe_type == MAC_BIBA_TYPE_EQUAL)
		return (0);

	/* If the range is low-high, it's ok. */
	if (mac_biba->mb_rangelow.mbe_type == MAC_BIBA_TYPE_LOW &&
	    mac_biba->mb_rangehigh.mbe_type == MAC_BIBA_TYPE_HIGH)
		return (0);

	/* It's not ok. */
	return (EPERM);
}

static int
mac_biba_high_effective(struct mac_biba *mac_biba)
{

	KASSERT((mac_biba->mb_flags & MAC_BIBA_FLAG_EFFECTIVE) != 0,
	    ("mac_biba_equal_effective: mac_biba not effective"));

	return (mac_biba->mb_effective.mbe_type == MAC_BIBA_TYPE_HIGH);
}

static int
mac_biba_valid(struct mac_biba *mac_biba)
{

	if (mac_biba->mb_flags & MAC_BIBA_FLAG_EFFECTIVE) {
		switch (mac_biba->mb_effective.mbe_type) {
		case MAC_BIBA_TYPE_GRADE:
			break;

		case MAC_BIBA_TYPE_EQUAL:
		case MAC_BIBA_TYPE_HIGH:
		case MAC_BIBA_TYPE_LOW:
			if (mac_biba->mb_effective.mbe_grade != 0 ||
			    !MAC_BIBA_BIT_SET_EMPTY(
			    mac_biba->mb_effective.mbe_compartments))
				return (EINVAL);
			break;

		default:
			return (EINVAL);
		}
	} else {
		if (mac_biba->mb_effective.mbe_type != MAC_BIBA_TYPE_UNDEF)
			return (EINVAL);
	}

	if (mac_biba->mb_flags & MAC_BIBA_FLAG_RANGE) {
		switch (mac_biba->mb_rangelow.mbe_type) {
		case MAC_BIBA_TYPE_GRADE:
			break;

		case MAC_BIBA_TYPE_EQUAL:
		case MAC_BIBA_TYPE_HIGH:
		case MAC_BIBA_TYPE_LOW:
			if (mac_biba->mb_rangelow.mbe_grade != 0 ||
			    !MAC_BIBA_BIT_SET_EMPTY(
			    mac_biba->mb_rangelow.mbe_compartments))
				return (EINVAL);
			break;

		default:
			return (EINVAL);
		}

		switch (mac_biba->mb_rangehigh.mbe_type) {
		case MAC_BIBA_TYPE_GRADE:
			break;

		case MAC_BIBA_TYPE_EQUAL:
		case MAC_BIBA_TYPE_HIGH:
		case MAC_BIBA_TYPE_LOW:
			if (mac_biba->mb_rangehigh.mbe_grade != 0 ||
			    !MAC_BIBA_BIT_SET_EMPTY(
			    mac_biba->mb_rangehigh.mbe_compartments))
				return (EINVAL);
			break;

		default:
			return (EINVAL);
		}
		if (!mac_biba_dominate_element(&mac_biba->mb_rangehigh,
		    &mac_biba->mb_rangelow))
			return (EINVAL);
	} else {
		if (mac_biba->mb_rangelow.mbe_type != MAC_BIBA_TYPE_UNDEF ||
		    mac_biba->mb_rangehigh.mbe_type != MAC_BIBA_TYPE_UNDEF)
			return (EINVAL);
	}

	return (0);
}

static void
mac_biba_set_range(struct mac_biba *mac_biba, u_short typelow,
    u_short gradelow, u_char *compartmentslow, u_short typehigh,
    u_short gradehigh, u_char *compartmentshigh)
{

	mac_biba->mb_rangelow.mbe_type = typelow;
	mac_biba->mb_rangelow.mbe_grade = gradelow;
	if (compartmentslow != NULL)
		memcpy(mac_biba->mb_rangelow.mbe_compartments,
		    compartmentslow,
		    sizeof(mac_biba->mb_rangelow.mbe_compartments));
	mac_biba->mb_rangehigh.mbe_type = typehigh;
	mac_biba->mb_rangehigh.mbe_grade = gradehigh;
	if (compartmentshigh != NULL)
		memcpy(mac_biba->mb_rangehigh.mbe_compartments,
		    compartmentshigh,
		    sizeof(mac_biba->mb_rangehigh.mbe_compartments));
	mac_biba->mb_flags |= MAC_BIBA_FLAG_RANGE;
}

static void
mac_biba_set_effective(struct mac_biba *mac_biba, u_short type, u_short grade,
    u_char *compartments)
{

	mac_biba->mb_effective.mbe_type = type;
	mac_biba->mb_effective.mbe_grade = grade;
	if (compartments != NULL)
		memcpy(mac_biba->mb_effective.mbe_compartments, compartments,
		    sizeof(mac_biba->mb_effective.mbe_compartments));
	mac_biba->mb_flags |= MAC_BIBA_FLAG_EFFECTIVE;
}

static void
mac_biba_copy_range(struct mac_biba *labelfrom, struct mac_biba *labelto)
{

	KASSERT((labelfrom->mb_flags & MAC_BIBA_FLAG_RANGE) != 0,
	    ("mac_biba_copy_range: labelfrom not range"));

	labelto->mb_rangelow = labelfrom->mb_rangelow;
	labelto->mb_rangehigh = labelfrom->mb_rangehigh;
	labelto->mb_flags |= MAC_BIBA_FLAG_RANGE;
}

static void
mac_biba_copy_effective(struct mac_biba *labelfrom, struct mac_biba *labelto)
{

	KASSERT((labelfrom->mb_flags & MAC_BIBA_FLAG_EFFECTIVE) != 0,
	    ("mac_biba_copy_effective: labelfrom not effective"));

	labelto->mb_effective = labelfrom->mb_effective;
	labelto->mb_flags |= MAC_BIBA_FLAG_EFFECTIVE;
}

static void
mac_biba_copy(struct mac_biba *source, struct mac_biba *dest)
{

	if (source->mb_flags & MAC_BIBA_FLAG_EFFECTIVE)
		mac_biba_copy_effective(source, dest);
	if (source->mb_flags & MAC_BIBA_FLAG_RANGE)
		mac_biba_copy_range(source, dest);
}

/*
 * Policy module operations.
 */
static void
mac_biba_init(struct mac_policy_conf *conf)
{

	zone_biba = uma_zcreate("mac_biba", sizeof(struct mac_biba), NULL,
	    NULL, NULL, NULL, UMA_ALIGN_PTR, 0);
}

/*
 * Label operations.
 */
static void
mac_biba_init_label(struct label *label)
{

	SLOT_SET(label, biba_alloc(M_WAITOK));
}

static int
mac_biba_init_label_waitcheck(struct label *label, int flag)
{

	SLOT_SET(label, biba_alloc(flag));
	if (SLOT(label) == NULL)
		return (ENOMEM);

	return (0);
}

static void
mac_biba_destroy_label(struct label *label)
{

	biba_free(SLOT(label));
	SLOT_SET(label, NULL);
}

/*
 * mac_biba_element_to_string() accepts an sbuf and Biba element.  It
 * converts the Biba element to a string and stores the result in the
 * sbuf; if there isn't space in the sbuf, -1 is returned.
 */
static int
mac_biba_element_to_string(struct sbuf *sb, struct mac_biba_element *element)
{
	int i, first;

	switch (element->mbe_type) {
	case MAC_BIBA_TYPE_HIGH:
		return (sbuf_printf(sb, "high"));

	case MAC_BIBA_TYPE_LOW:
		return (sbuf_printf(sb, "low"));

	case MAC_BIBA_TYPE_EQUAL:
		return (sbuf_printf(sb, "equal"));

	case MAC_BIBA_TYPE_GRADE:
		if (sbuf_printf(sb, "%d", element->mbe_grade) == -1)
			return (-1);

		first = 1;
		for (i = 1; i <= MAC_BIBA_MAX_COMPARTMENTS; i++) {
			if (MAC_BIBA_BIT_TEST(i, element->mbe_compartments)) {
				if (first) {
					if (sbuf_putc(sb, ':') == -1)
						return (-1);
					if (sbuf_printf(sb, "%d", i) == -1)
						return (-1);
					first = 0;
				} else {
					if (sbuf_printf(sb, "+%d", i) == -1)
						return (-1);
				}
			}
		}
		return (0);

	default:
		panic("mac_biba_element_to_string: invalid type (%d)",
		    element->mbe_type);
	}
}

/*
 * mac_biba_to_string() converts a Biba label to a string, and places
 * the results in the passed sbuf.  It returns 0 on success, or EINVAL
 * if there isn't room in the sbuf.  Note: the sbuf will be modified
 * even in a failure case, so the caller may need to revert the sbuf
 * by restoring the offset if that's undesired.
 */
static int
mac_biba_to_string(struct sbuf *sb, struct mac_biba *mac_biba)
{

	if (mac_biba->mb_flags & MAC_BIBA_FLAG_EFFECTIVE) {
		if (mac_biba_element_to_string(sb, &mac_biba->mb_effective)
		    == -1)
			return (EINVAL);
	}

	if (mac_biba->mb_flags & MAC_BIBA_FLAG_RANGE) {
		if (sbuf_putc(sb, '(') == -1)
			return (EINVAL);

		if (mac_biba_element_to_string(sb, &mac_biba->mb_rangelow)
		    == -1)
			return (EINVAL);

		if (sbuf_putc(sb, '-') == -1)
			return (EINVAL);

		if (mac_biba_element_to_string(sb, &mac_biba->mb_rangehigh)
		    == -1)
			return (EINVAL);

		if (sbuf_putc(sb, ')') == -1)
			return (EINVAL);
	}

	return (0);
}

static int
mac_biba_externalize_label(struct label *label, char *element_name,
    struct sbuf *sb, int *claimed)
{
	struct mac_biba *mac_biba;

	if (strcmp(MAC_BIBA_LABEL_NAME, element_name) != 0)
		return (0);

	(*claimed)++;

	mac_biba = SLOT(label);
	return (mac_biba_to_string(sb, mac_biba));
}

static int
mac_biba_parse_element(struct mac_biba_element *element, char *string)
{
	char *compartment, *end, *grade;
	int value;

	if (strcmp(string, "high") == 0 ||
	    strcmp(string, "hi") == 0) {
		element->mbe_type = MAC_BIBA_TYPE_HIGH;
		element->mbe_grade = MAC_BIBA_TYPE_UNDEF;
	} else if (strcmp(string, "low") == 0 ||
	    strcmp(string, "lo") == 0) {
		element->mbe_type = MAC_BIBA_TYPE_LOW;
		element->mbe_grade = MAC_BIBA_TYPE_UNDEF;
	} else if (strcmp(string, "equal") == 0 ||
	    strcmp(string, "eq") == 0) {
		element->mbe_type = MAC_BIBA_TYPE_EQUAL;
		element->mbe_grade = MAC_BIBA_TYPE_UNDEF;
	} else {
		element->mbe_type = MAC_BIBA_TYPE_GRADE;

		/*
		 * Numeric grade piece of the element.
		 */
		grade = strsep(&string, ":");
		value = strtol(grade, &end, 10);
		if (end == grade || *end != '\0')
			return (EINVAL);
		if (value < 0 || value > 65535)
			return (EINVAL);
		element->mbe_grade = value;

		/*
		 * Optional compartment piece of the element.  If none
		 * are included, we assume that the label has no
		 * compartments.
		 */
		if (string == NULL)
			return (0);
		if (*string == '\0')
			return (0);

		while ((compartment = strsep(&string, "+")) != NULL) {
			value = strtol(compartment, &end, 10);
			if (compartment == end || *end != '\0')
				return (EINVAL);
			if (value < 1 || value > MAC_BIBA_MAX_COMPARTMENTS)
				return (EINVAL);
			MAC_BIBA_BIT_SET(value, element->mbe_compartments);
		}
	}

	return (0);
}

/*
 * Note: destructively consumes the string, make a local copy before
 * calling if that's a problem.
 */
static int
mac_biba_parse(struct mac_biba *mac_biba, char *string)
{
	char *rangehigh, *rangelow, *effective;
	int error;

	effective = strsep(&string, "(");
	if (*effective == '\0')
		effective = NULL;

	if (string != NULL) {
		rangelow = strsep(&string, "-");
		if (string == NULL)
			return (EINVAL);
		rangehigh = strsep(&string, ")");
		if (string == NULL)
			return (EINVAL);
		if (*string != '\0')
			return (EINVAL);
	} else {
		rangelow = NULL;
		rangehigh = NULL;
	}

	KASSERT((rangelow != NULL && rangehigh != NULL) ||
	    (rangelow == NULL && rangehigh == NULL),
	    ("mac_biba_parse: range mismatch"));

	bzero(mac_biba, sizeof(*mac_biba));
	if (effective != NULL) {
		error = mac_biba_parse_element(&mac_biba->mb_effective, effective);
		if (error)
			return (error);
		mac_biba->mb_flags |= MAC_BIBA_FLAG_EFFECTIVE;
	}

	if (rangelow != NULL) {
		error = mac_biba_parse_element(&mac_biba->mb_rangelow,
		    rangelow);
		if (error)
			return (error);
		error = mac_biba_parse_element(&mac_biba->mb_rangehigh,
		    rangehigh);
		if (error)
			return (error);
		mac_biba->mb_flags |= MAC_BIBA_FLAG_RANGE;
	}

	error = mac_biba_valid(mac_biba);
	if (error)
		return (error);

	return (0);
}

static int
mac_biba_internalize_label(struct label *label, char *element_name,
    char *element_data, int *claimed)
{
	struct mac_biba *mac_biba, mac_biba_temp;
	int error;

	if (strcmp(MAC_BIBA_LABEL_NAME, element_name) != 0)
		return (0);

	(*claimed)++;

	error = mac_biba_parse(&mac_biba_temp, element_data);
	if (error)
		return (error);

	mac_biba = SLOT(label);
	*mac_biba = mac_biba_temp;

	return (0);
}

static void
mac_biba_copy_label(struct label *src, struct label *dest)
{

	*SLOT(dest) = *SLOT(src);
}

/*
 * Labeling event operations: file system objects, and things that look
 * a lot like file system objects.
 */
static void
mac_biba_devfs_create_device(struct ucred *cred, struct mount *mp,
    struct cdev *dev, struct devfs_dirent *de, struct label *delabel)
{
	struct mac_biba *mac_biba;
	int biba_type;

	mac_biba = SLOT(delabel);
	if (strcmp(dev->si_name, "null") == 0 ||
	    strcmp(dev->si_name, "zero") == 0 ||
	    strcmp(dev->si_name, "random") == 0 ||
	    strncmp(dev->si_name, "fd/", strlen("fd/")) == 0)
		biba_type = MAC_BIBA_TYPE_EQUAL;
	else if (ptys_equal &&
	    (strncmp(dev->si_name, "ttyp", strlen("ttyp")) == 0 ||
	    strncmp(dev->si_name, "ptyp", strlen("ptyp")) == 0))
		biba_type = MAC_BIBA_TYPE_EQUAL;
	else
		biba_type = MAC_BIBA_TYPE_HIGH;
	mac_biba_set_effective(mac_biba, biba_type, 0, NULL);
}

static void
mac_biba_devfs_create_directory(struct mount *mp, char *dirname,
    int dirnamelen, struct devfs_dirent *de, struct label *delabel)
{
	struct mac_biba *mac_biba;

	mac_biba = SLOT(delabel);
	mac_biba_set_effective(mac_biba, MAC_BIBA_TYPE_HIGH, 0, NULL);
}

static void
mac_biba_devfs_create_symlink(struct ucred *cred, struct mount *mp,
    struct devfs_dirent *dd, struct label *ddlabel, struct devfs_dirent *de,
    struct label *delabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(cred->cr_label);
	dest = SLOT(delabel);

	mac_biba_copy_effective(source, dest);
}

static void
mac_biba_mount_create(struct ucred *cred, struct mount *mp,
    struct label *mplabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(cred->cr_label);
	dest = SLOT(mplabel);
	mac_biba_copy_effective(source, dest);
}

static void
mac_biba_vnode_relabel(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, struct label *newlabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(newlabel);
	dest = SLOT(vplabel);

	mac_biba_copy(source, dest);
}

static void
mac_biba_devfs_update(struct mount *mp, struct devfs_dirent *de,
    struct label *delabel, struct vnode *vp, struct label *vplabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(vplabel);
	dest = SLOT(delabel);

	mac_biba_copy(source, dest);
}

static void
mac_biba_devfs_vnode_associate(struct mount *mp, struct label *mntlabel,
    struct devfs_dirent *de, struct label *delabel, struct vnode *vp,
    struct label *vplabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(delabel);
	dest = SLOT(vplabel);

	mac_biba_copy_effective(source, dest);
}

static int
mac_biba_vnode_associate_extattr(struct mount *mp, struct label *mplabel,
    struct vnode *vp, struct label *vplabel)
{
	struct mac_biba temp, *source, *dest;
	int buflen, error;

	source = SLOT(mplabel);
	dest = SLOT(vplabel);

	buflen = sizeof(temp);
	bzero(&temp, buflen);

	error = vn_extattr_get(vp, IO_NODELOCKED, MAC_BIBA_EXTATTR_NAMESPACE,
	    MAC_BIBA_EXTATTR_NAME, &buflen, (char *) &temp, curthread);
	if (error == ENOATTR || error == EOPNOTSUPP) {
		/* Fall back to the mntlabel. */
		mac_biba_copy_effective(source, dest);
		return (0);
	} else if (error)
		return (error);

	if (buflen != sizeof(temp)) {
		printf("mac_biba_vnode_associate_extattr: bad size %d\n",
		    buflen);
		return (EPERM);
	}
	if (mac_biba_valid(&temp) != 0) {
		printf("mac_biba_vnode_associate_extattr: invalid\n");
		return (EPERM);
	}
	if ((temp.mb_flags & MAC_BIBA_FLAGS_BOTH) != MAC_BIBA_FLAG_EFFECTIVE) {
		printf("mac_biba_vnode_associate_extattr: not effective\n");
		return (EPERM);
	}

	mac_biba_copy_effective(&temp, dest);
	return (0);
}

static void
mac_biba_vnode_associate_singlelabel(struct mount *mp,
    struct label *mplabel, struct vnode *vp, struct label *vplabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(mplabel);
	dest = SLOT(vplabel);

	mac_biba_copy_effective(source, dest);
}

static int
mac_biba_vnode_create_extattr(struct ucred *cred, struct mount *mp,
    struct label *mplabel, struct vnode *dvp, struct label *dvplabel,
    struct vnode *vp, struct label *vplabel, struct componentname *cnp)
{
	struct mac_biba *source, *dest, temp;
	size_t buflen;
	int error;

	buflen = sizeof(temp);
	bzero(&temp, buflen);

	source = SLOT(cred->cr_label);
	dest = SLOT(vplabel);
	mac_biba_copy_effective(source, &temp);

	error = vn_extattr_set(vp, IO_NODELOCKED, MAC_BIBA_EXTATTR_NAMESPACE,
	    MAC_BIBA_EXTATTR_NAME, buflen, (char *) &temp, curthread);
	if (error == 0)
		mac_biba_copy_effective(source, dest);
	return (error);
}

static int
mac_biba_vnode_setlabel_extattr(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, struct label *intlabel)
{
	struct mac_biba *source, temp;
	size_t buflen;
	int error;

	buflen = sizeof(temp);
	bzero(&temp, buflen);

	source = SLOT(intlabel);
	if ((source->mb_flags & MAC_BIBA_FLAG_EFFECTIVE) == 0)
		return (0);

	mac_biba_copy_effective(source, &temp);

	error = vn_extattr_set(vp, IO_NODELOCKED, MAC_BIBA_EXTATTR_NAMESPACE,
	    MAC_BIBA_EXTATTR_NAME, buflen, (char *) &temp, curthread);
	return (error);
}

/*
 * Labeling event operations: IPC object.
 */
static void
mac_biba_inpcb_create(struct socket *so, struct label *solabel,
    struct inpcb *inp, struct label *inplabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(solabel);
	dest = SLOT(inplabel);

	mac_biba_copy_effective(source, dest);
}

static void
mac_biba_socket_create_mbuf(struct socket *so, struct label *solabel,
    struct mbuf *m, struct label *mlabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(solabel);
	dest = SLOT(mlabel);

	mac_biba_copy_effective(source, dest);
}

static void
mac_biba_socket_create(struct ucred *cred, struct socket *so,
    struct label *solabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(cred->cr_label);
	dest = SLOT(solabel);

	mac_biba_copy_effective(source, dest);
}

static void
mac_biba_pipe_create(struct ucred *cred, struct pipepair *pp,
    struct label *pplabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(cred->cr_label);
	dest = SLOT(pplabel);

	mac_biba_copy_effective(source, dest);
}

static void
mac_biba_posixsem_create(struct ucred *cred, struct ksem *ks,
    struct label *kslabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(cred->cr_label);
	dest = SLOT(kslabel);

	mac_biba_copy_effective(source, dest);
}

static void
mac_biba_socket_newconn(struct socket *oldso, struct label *oldsolabel,
    struct socket *newso, struct label *newsolabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(oldsolabel);
	dest = SLOT(newsolabel);

	mac_biba_copy_effective(source, dest);
}

static void
mac_biba_socket_relabel(struct ucred *cred, struct socket *so,
    struct label *solabel, struct label *newlabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(newlabel);
	dest = SLOT(solabel);

	mac_biba_copy(source, dest);
}

static void
mac_biba_pipe_relabel(struct ucred *cred, struct pipepair *pp,
    struct label *pplabel, struct label *newlabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(newlabel);
	dest = SLOT(pplabel);

	mac_biba_copy(source, dest);
}

static void
mac_biba_socketpeer_set_from_mbuf(struct mbuf *m, struct label *mlabel,
    struct socket *so, struct label *sopeerlabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(mlabel);
	dest = SLOT(sopeerlabel);

	mac_biba_copy_effective(source, dest);
}

/*
 * Labeling event operations: System V IPC objects.
 */
static void
mac_biba_sysvmsg_create(struct ucred *cred, struct msqid_kernel *msqkptr,
    struct label *msqlabel, struct msg *msgptr, struct label *msglabel)
{
	struct mac_biba *source, *dest;

	/* Ignore the msgq label */
	source = SLOT(cred->cr_label);
	dest = SLOT(msglabel);

	mac_biba_copy_effective(source, dest);
}

static void
mac_biba_sysvmsq_create(struct ucred *cred,
    struct msqid_kernel *msqkptr, struct label *msqlabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(cred->cr_label);
	dest = SLOT(msqlabel);

	mac_biba_copy_effective(source, dest);
}

static void
mac_biba_sysvsem_create(struct ucred *cred, struct semid_kernel *semakptr,
    struct label *semalabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(cred->cr_label);
	dest = SLOT(semalabel);

	mac_biba_copy_effective(source, dest);
}

static void
mac_biba_sysvshm_create(struct ucred *cred, struct shmid_kernel *shmsegptr,
    struct label *shmlabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(cred->cr_label);
	dest = SLOT(shmlabel);

	mac_biba_copy_effective(source, dest);
}

/*
 * Labeling event operations: network objects.
 */
static void
mac_biba_socketpeer_set_from_socket(struct socket *oldso,
    struct label *oldsolabel, struct socket *newso,
    struct label *newsopeerlabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(oldsolabel);
	dest = SLOT(newsopeerlabel);

	mac_biba_copy_effective(source, dest);
}

static void
mac_biba_bpfdesc_create(struct ucred *cred, struct bpf_d *d,
    struct label *dlabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(cred->cr_label);
	dest = SLOT(dlabel);

	mac_biba_copy_effective(source, dest);
}

static void
mac_biba_ifnet_create(struct ifnet *ifp, struct label *ifplabel)
{
	char tifname[IFNAMSIZ], *p, *q;
	char tiflist[sizeof(trusted_interfaces)];
	struct mac_biba *dest;
	int len, type;

	dest = SLOT(ifplabel);

	if (ifp->if_type == IFT_LOOP || interfaces_equal != 0) {
		type = MAC_BIBA_TYPE_EQUAL;
		goto set;
	}

	if (trust_all_interfaces) {
		type = MAC_BIBA_TYPE_HIGH;
		goto set;
	}

	type = MAC_BIBA_TYPE_LOW;

	if (trusted_interfaces[0] == '\0' ||
	    !strvalid(trusted_interfaces, sizeof(trusted_interfaces)))
		goto set;

	bzero(tiflist, sizeof(tiflist));
	for (p = trusted_interfaces, q = tiflist; *p != '\0'; p++, q++)
		if(*p != ' ' && *p != '\t')
			*q = *p;

	for (p = q = tiflist;; p++) {
		if (*p == ',' || *p == '\0') {
			len = p - q;
			if (len < IFNAMSIZ) {
				bzero(tifname, sizeof(tifname));
				bcopy(q, tifname, len);
				if (strcmp(tifname, ifp->if_xname) == 0) {
					type = MAC_BIBA_TYPE_HIGH;
					break;
				}
			} else {
				*p = '\0';
				printf("mac_biba warning: interface name "
				    "\"%s\" is too long (must be < %d)\n",
				    q, IFNAMSIZ);
			}
			if (*p == '\0')
				break;
			q = p + 1;
		}
	}
set:
	mac_biba_set_effective(dest, type, 0, NULL);
	mac_biba_set_range(dest, type, 0, NULL, type, 0, NULL);
}

static void
mac_biba_ipq_create(struct mbuf *m, struct label *mlabel, struct ipq *ipq,
    struct label *ipqlabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(mlabel);
	dest = SLOT(ipqlabel);

	mac_biba_copy_effective(source, dest);
}

static void
mac_biba_ipq_reassemble(struct ipq *ipq, struct label *ipqlabel,
    struct mbuf *m, struct label *mlabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(ipqlabel);
	dest = SLOT(mlabel);

	/* Just use the head, since we require them all to match. */
	mac_biba_copy_effective(source, dest);
}

static void
mac_biba_netinet_fragment(struct mbuf *m, struct label *mlabel,
    struct mbuf *frag, struct label *fraglabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(mlabel);
	dest = SLOT(fraglabel);

	mac_biba_copy_effective(source, dest);
}

static void
mac_biba_inpcb_create_mbuf(struct inpcb *inp, struct label *inplabel,
    struct mbuf *m, struct label *mlabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(inplabel);
	dest = SLOT(mlabel);

	mac_biba_copy_effective(source, dest);
}

static void
mac_biba_create_mbuf_linklayer(struct ifnet *ifp, struct label *ifplabel,
    struct mbuf *m, struct label *mlabel)
{
	struct mac_biba *dest;

	dest = SLOT(mlabel);

	mac_biba_set_effective(dest, MAC_BIBA_TYPE_EQUAL, 0, NULL);
}

static void
mac_biba_bpfdesc_create_mbuf(struct bpf_d *d, struct label *dlabel,
    struct mbuf *m, struct label *mlabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(dlabel);
	dest = SLOT(mlabel);

	mac_biba_copy_effective(source, dest);
}

static void
mac_biba_ifnet_create_mbuf(struct ifnet *ifp, struct label *ifplabel,
    struct mbuf *m, struct label *mlabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(ifplabel);
	dest = SLOT(mlabel);

	mac_biba_copy_effective(source, dest);
}

static void
mac_biba_mbuf_create_multicast_encap(struct mbuf *m, struct label *mlabel,
    struct ifnet *ifp, struct label *ifplabel, struct mbuf *mnew,
    struct label *mnewlabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(mlabel);
	dest = SLOT(mnewlabel);

	mac_biba_copy_effective(source, dest);
}

static void
mac_biba_mbuf_create_netlayer(struct mbuf *m, struct label *mlabel,
    struct mbuf *newm, struct label *mnewlabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(mlabel);
	dest = SLOT(mnewlabel);

	mac_biba_copy_effective(source, dest);
}

static int
mac_biba_ipq_match(struct mbuf *m, struct label *mlabel, struct ipq *ipq,
    struct label *ipqlabel)
{
	struct mac_biba *a, *b;

	a = SLOT(ipqlabel);
	b = SLOT(mlabel);

	return (mac_biba_equal_effective(a, b));
}

static void
mac_biba_ifnet_relabel(struct ucred *cred, struct ifnet *ifp,
    struct label *ifplabel, struct label *newlabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(newlabel);
	dest = SLOT(ifplabel);

	mac_biba_copy(source, dest);
}

static void
mac_biba_ipq_update(struct mbuf *m, struct label *mlabel, struct ipq *ipq,
    struct label *ipqlabel)
{

	/* NOOP: we only accept matching labels, so no need to update */
}

static void
mac_biba_inpcb_sosetlabel(struct socket *so, struct label *solabel,
    struct inpcb *inp, struct label *inplabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(solabel);
	dest = SLOT(inplabel);

	mac_biba_copy(source, dest);
}

static void
mac_biba_mbuf_create_from_firewall(struct mbuf *m, struct label *label)
{
	struct mac_biba *dest;

	dest = SLOT(label);

	/* XXX: where is the label for the firewall really comming from? */
	mac_biba_set_effective(dest, MAC_BIBA_TYPE_EQUAL, 0, NULL);
}

/*
 * Labeling event operations: processes.
 */
static void
mac_biba_proc_create_swapper(struct ucred *cred)
{
	struct mac_biba *dest;

	dest = SLOT(cred->cr_label);

	mac_biba_set_effective(dest, MAC_BIBA_TYPE_EQUAL, 0, NULL);
	mac_biba_set_range(dest, MAC_BIBA_TYPE_LOW, 0, NULL,
	    MAC_BIBA_TYPE_HIGH, 0, NULL);
}

static void
mac_biba_proc_create_init(struct ucred *cred)
{
	struct mac_biba *dest;

	dest = SLOT(cred->cr_label);

	mac_biba_set_effective(dest, MAC_BIBA_TYPE_HIGH, 0, NULL);
	mac_biba_set_range(dest, MAC_BIBA_TYPE_LOW, 0, NULL,
	    MAC_BIBA_TYPE_HIGH, 0, NULL);
}

static void
mac_biba_cred_relabel(struct ucred *cred, struct label *newlabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(newlabel);
	dest = SLOT(cred->cr_label);

	mac_biba_copy(source, dest);
}

/*
 * Label cleanup/flush operations
 */
static void
mac_biba_sysvmsg_cleanup(struct label *msglabel)
{

	bzero(SLOT(msglabel), sizeof(struct mac_biba));
}

static void
mac_biba_sysvmsq_cleanup(struct label *msqlabel)
{

	bzero(SLOT(msqlabel), sizeof(struct mac_biba));
}

static void
mac_biba_sysvsem_cleanup(struct label *semalabel)
{

	bzero(SLOT(semalabel), sizeof(struct mac_biba));
}

static void
mac_biba_sysvshm_cleanup(struct label *shmlabel)
{
	bzero(SLOT(shmlabel), sizeof(struct mac_biba));
}

/*
 * Access control checks.
 */
static int
mac_biba_bpfdesc_check_receive(struct bpf_d *d, struct label *dlabel,
    struct ifnet *ifp, struct label *ifplabel)
{
	struct mac_biba *a, *b;

	if (!mac_biba_enabled)
		return (0);

	a = SLOT(dlabel);
	b = SLOT(ifplabel);

	if (mac_biba_equal_effective(a, b))
		return (0);
	return (EACCES);
}

static int
mac_biba_cred_check_relabel(struct ucred *cred, struct label *newlabel)
{
	struct mac_biba *subj, *new;
	int error;

	subj = SLOT(cred->cr_label);
	new = SLOT(newlabel);

	/*
	 * If there is a Biba label update for the credential, it may
	 * be an update of the effective, range, or both.
	 */
	error = biba_atmostflags(new, MAC_BIBA_FLAGS_BOTH);
	if (error)
		return (error);

	/*
	 * If the Biba label is to be changed, authorize as appropriate.
	 */
	if (new->mb_flags & MAC_BIBA_FLAGS_BOTH) {
		/*
		 * If the change request modifies both the Biba label
		 * effective and range, check that the new effective will be
		 * in the new range.
		 */
		if ((new->mb_flags & MAC_BIBA_FLAGS_BOTH) ==
		    MAC_BIBA_FLAGS_BOTH &&
		    !mac_biba_effective_in_range(new, new))
			return (EINVAL);

		/*
		 * To change the Biba effective label on a credential, the
		 * new effective label must be in the current range.
		 */
		if (new->mb_flags & MAC_BIBA_FLAG_EFFECTIVE &&
		    !mac_biba_effective_in_range(new, subj))
			return (EPERM);

		/*
		 * To change the Biba range on a credential, the new
		 * range label must be in the current range.
		 */
		if (new->mb_flags & MAC_BIBA_FLAG_RANGE &&
		    !mac_biba_range_in_range(new, subj))
			return (EPERM);

		/*
		 * To have EQUAL in any component of the new credential
		 * Biba label, the subject must already have EQUAL in
		 * their label.
		 */
		if (mac_biba_contains_equal(new)) {
			error = mac_biba_subject_privileged(subj);
			if (error)
				return (error);
		}
	}

	return (0);
}

static int
mac_biba_cred_check_visible(struct ucred *u1, struct ucred *u2)
{
	struct mac_biba *subj, *obj;

	if (!mac_biba_enabled)
		return (0);

	subj = SLOT(u1->cr_label);
	obj = SLOT(u2->cr_label);

	/* XXX: range */
	if (!mac_biba_dominate_effective(obj, subj))
		return (ESRCH);

	return (0);
}

static int
mac_biba_ifnet_check_relabel(struct ucred *cred, struct ifnet *ifp,
    struct label *ifplabel, struct label *newlabel)
{
	struct mac_biba *subj, *new;
	int error;

	subj = SLOT(cred->cr_label);
	new = SLOT(newlabel);

	/*
	 * If there is a Biba label update for the interface, it may
	 * be an update of the effective, range, or both.
	 */
	error = biba_atmostflags(new, MAC_BIBA_FLAGS_BOTH);
	if (error)
		return (error);

	/*
	 * Relabling network interfaces requires Biba privilege.
	 */
	error = mac_biba_subject_privileged(subj);
	if (error)
		return (error);

	return (0);
}

static int
mac_biba_ifnet_check_transmit(struct ifnet *ifp, struct label *ifplabel,
    struct mbuf *m, struct label *mlabel)
{
	struct mac_biba *p, *i;

	if (!mac_biba_enabled)
		return (0);

	p = SLOT(mlabel);
	i = SLOT(ifplabel);

	return (mac_biba_effective_in_range(p, i) ? 0 : EACCES);
}

static int
mac_biba_inpcb_check_deliver(struct inpcb *inp, struct label *inplabel,
    struct mbuf *m, struct label *mlabel)
{
	struct mac_biba *p, *i;

	if (!mac_biba_enabled)
		return (0);

	p = SLOT(mlabel);
	i = SLOT(inplabel);

	return (mac_biba_equal_effective(p, i) ? 0 : EACCES);
}

static int
mac_biba_sysvmsq_check_msgrcv(struct ucred *cred, struct msg *msgptr,
    struct label *msglabel)
{
	struct mac_biba *subj, *obj;

	if (!mac_biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(msglabel);

	if (!mac_biba_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
mac_biba_sysvmsq_check_msgrmid(struct ucred *cred, struct msg *msgptr,
    struct label *msglabel)
{
	struct mac_biba *subj, *obj;

	if (!mac_biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(msglabel);

	if (!mac_biba_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
mac_biba_sysvmsq_check_msqget(struct ucred *cred,
    struct msqid_kernel *msqkptr, struct label *msqklabel)
{
	struct mac_biba *subj, *obj;

	if (!mac_biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(msqklabel);

	if (!mac_biba_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
mac_biba_sysvmsq_check_msqsnd(struct ucred *cred,
    struct msqid_kernel *msqkptr, struct label *msqklabel)
{
	struct mac_biba *subj, *obj;

	if (!mac_biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(msqklabel);

	if (!mac_biba_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
mac_biba_sysvmsq_check_msqrcv(struct ucred *cred,
    struct msqid_kernel *msqkptr, struct label *msqklabel)
{
	struct mac_biba *subj, *obj;

	if (!mac_biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(msqklabel);

	if (!mac_biba_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}


static int
mac_biba_sysvmsq_check_msqctl(struct ucred *cred,
    struct msqid_kernel *msqkptr, struct label *msqklabel, int cmd)
{
	struct mac_biba *subj, *obj;

	if (!mac_biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(msqklabel);

	switch(cmd) {
	case IPC_RMID:
	case IPC_SET:
		if (!mac_biba_dominate_effective(subj, obj))
			return (EACCES);
		break;

	case IPC_STAT:
		if (!mac_biba_dominate_effective(obj, subj))
			return (EACCES);
		break;

	default:
		return (EACCES);
	}

	return (0);
}

static int
mac_biba_sysvsem_check_semctl(struct ucred *cred,
    struct semid_kernel *semakptr, struct label *semaklabel, int cmd)
{
	struct mac_biba *subj, *obj;

	if (!mac_biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(semaklabel);

	switch(cmd) {
	case IPC_RMID:
	case IPC_SET:
	case SETVAL:
	case SETALL:
		if (!mac_biba_dominate_effective(subj, obj))
			return (EACCES);
		break;

	case IPC_STAT:
	case GETVAL:
	case GETPID:
	case GETNCNT:
	case GETZCNT:
	case GETALL:
		if (!mac_biba_dominate_effective(obj, subj))
			return (EACCES);
		break;

	default:
		return (EACCES);
	}

	return (0);
}

static int
mac_biba_sysvsem_check_semget(struct ucred *cred,
    struct semid_kernel *semakptr, struct label *semaklabel)
{
	struct mac_biba *subj, *obj;

	if (!mac_biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(semaklabel);

	if (!mac_biba_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}


static int
mac_biba_sysvsem_check_semop(struct ucred *cred,
    struct semid_kernel *semakptr, struct label *semaklabel,
    size_t accesstype)
{
	struct mac_biba *subj, *obj;

	if (!mac_biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(semaklabel);

	if (accesstype & SEM_R)
		if (!mac_biba_dominate_effective(obj, subj))
			return (EACCES);

	if (accesstype & SEM_A)
		if (!mac_biba_dominate_effective(subj, obj))
			return (EACCES);

	return (0);
}

static int
mac_biba_sysvshm_check_shmat(struct ucred *cred,
    struct shmid_kernel *shmsegptr, struct label *shmseglabel, int shmflg)
{
	struct mac_biba *subj, *obj;

	if (!mac_biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(shmseglabel);

	if (!mac_biba_dominate_effective(obj, subj))
		return (EACCES);
	if ((shmflg & SHM_RDONLY) == 0) {
		if (!mac_biba_dominate_effective(subj, obj))
			return (EACCES);
	}
	
	return (0);
}

static int
mac_biba_sysvshm_check_shmctl(struct ucred *cred,
    struct shmid_kernel *shmsegptr, struct label *shmseglabel, int cmd)
{
	struct mac_biba *subj, *obj;

	if (!mac_biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(shmseglabel);

	switch(cmd) {
	case IPC_RMID:
	case IPC_SET:
		if (!mac_biba_dominate_effective(subj, obj))
			return (EACCES);
		break;

	case IPC_STAT:
	case SHM_STAT:
		if (!mac_biba_dominate_effective(obj, subj))
			return (EACCES);
		break;

	default:
		return (EACCES);
	}

	return (0);
}

static int
mac_biba_sysvshm_check_shmget(struct ucred *cred,
    struct shmid_kernel *shmsegptr, struct label *shmseglabel, int shmflg)
{
	struct mac_biba *subj, *obj;

	if (!mac_biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(shmseglabel);

	if (!mac_biba_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
mac_biba_kld_check_load(struct ucred *cred, struct vnode *vp,
    struct label *vplabel)
{
	struct mac_biba *subj, *obj;
	int error;

	if (!mac_biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);

	error = mac_biba_subject_privileged(subj);
	if (error)
		return (error);

	obj = SLOT(vplabel);
	if (!mac_biba_high_effective(obj))
		return (EACCES);

	return (0);
}

static int
mac_biba_mount_check_stat(struct ucred *cred, struct mount *mp,
    struct label *mplabel)
{
	struct mac_biba *subj, *obj;

	if (!mac_biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(mplabel);

	if (!mac_biba_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
mac_biba_pipe_check_ioctl(struct ucred *cred, struct pipepair *pp,
    struct label *pplabel, unsigned long cmd, void /* caddr_t */ *data)
{

	if(!mac_biba_enabled)
		return (0);

	/* XXX: This will be implemented soon... */

	return (0);
}

static int
mac_biba_pipe_check_poll(struct ucred *cred, struct pipepair *pp,
    struct label *pplabel)
{
	struct mac_biba *subj, *obj;

	if (!mac_biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(pplabel);

	if (!mac_biba_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
mac_biba_pipe_check_read(struct ucred *cred, struct pipepair *pp,
    struct label *pplabel)
{
	struct mac_biba *subj, *obj;

	if (!mac_biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(pplabel);

	if (!mac_biba_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
mac_biba_pipe_check_relabel(struct ucred *cred, struct pipepair *pp,
    struct label *pplabel, struct label *newlabel)
{
	struct mac_biba *subj, *obj, *new;
	int error;

	new = SLOT(newlabel);
	subj = SLOT(cred->cr_label);
	obj = SLOT(pplabel);

	/*
	 * If there is a Biba label update for a pipe, it must be a
	 * effective update.
	 */
	error = biba_atmostflags(new, MAC_BIBA_FLAG_EFFECTIVE);
	if (error)
		return (error);

	/*
	 * To perform a relabel of a pipe (Biba label or not), Biba must
	 * authorize the relabel.
	 */
	if (!mac_biba_effective_in_range(obj, subj))
		return (EPERM);

	/*
	 * If the Biba label is to be changed, authorize as appropriate.
	 */
	if (new->mb_flags & MAC_BIBA_FLAG_EFFECTIVE) {
		/*
		 * To change the Biba label on a pipe, the new pipe label
		 * must be in the subject range.
		 */
		if (!mac_biba_effective_in_range(new, subj))
			return (EPERM);

		/*
		 * To change the Biba label on a pipe to be EQUAL, the
		 * subject must have appropriate privilege.
		 */
		if (mac_biba_contains_equal(new)) {
			error = mac_biba_subject_privileged(subj);
			if (error)
				return (error);
		}
	}

	return (0);
}

static int
mac_biba_pipe_check_stat(struct ucred *cred, struct pipepair *pp,
    struct label *pplabel)
{
	struct mac_biba *subj, *obj;

	if (!mac_biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(pplabel);

	if (!mac_biba_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
mac_biba_pipe_check_write(struct ucred *cred, struct pipepair *pp,
    struct label *pplabel)
{
	struct mac_biba *subj, *obj;

	if (!mac_biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(pplabel);

	if (!mac_biba_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
mac_biba_posixsem_check_write(struct ucred *cred, struct ksem *ks,
    struct label *kslabel)
{
	struct mac_biba *subj, *obj;

	if (!mac_biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(kslabel);

	if (!mac_biba_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
mac_biba_posixsem_check_rdonly(struct ucred *cred, struct ksem *ks,
    struct label *kslabel)
{
	struct mac_biba *subj, *obj;

	if (!mac_biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(kslabel);

	if (!mac_biba_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
mac_biba_proc_check_debug(struct ucred *cred, struct proc *p)
{
	struct mac_biba *subj, *obj;

	if (!mac_biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(p->p_ucred->cr_label);

	/* XXX: range checks */
	if (!mac_biba_dominate_effective(obj, subj))
		return (ESRCH);
	if (!mac_biba_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
mac_biba_proc_check_sched(struct ucred *cred, struct proc *p)
{
	struct mac_biba *subj, *obj;

	if (!mac_biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(p->p_ucred->cr_label);

	/* XXX: range checks */
	if (!mac_biba_dominate_effective(obj, subj))
		return (ESRCH);
	if (!mac_biba_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
mac_biba_proc_check_signal(struct ucred *cred, struct proc *p, int signum)
{
	struct mac_biba *subj, *obj;

	if (!mac_biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(p->p_ucred->cr_label);

	/* XXX: range checks */
	if (!mac_biba_dominate_effective(obj, subj))
		return (ESRCH);
	if (!mac_biba_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
mac_biba_socket_check_deliver(struct socket *so, struct label *solabel,
    struct mbuf *m, struct label *mlabel)
{
	struct mac_biba *p, *s;

	if (!mac_biba_enabled)
		return (0);

	p = SLOT(mlabel);
	s = SLOT(solabel);

	return (mac_biba_equal_effective(p, s) ? 0 : EACCES);
}

static int
mac_biba_socket_check_relabel(struct ucred *cred, struct socket *so,
    struct label *solabel, struct label *newlabel)
{
	struct mac_biba *subj, *obj, *new;
	int error;

	new = SLOT(newlabel);
	subj = SLOT(cred->cr_label);
	obj = SLOT(solabel);

	/*
	 * If there is a Biba label update for the socket, it may be
	 * an update of effective.
	 */
	error = biba_atmostflags(new, MAC_BIBA_FLAG_EFFECTIVE);
	if (error)
		return (error);

	/*
	 * To relabel a socket, the old socket effective must be in the subject
	 * range.
	 */
	if (!mac_biba_effective_in_range(obj, subj))
		return (EPERM);

	/*
	 * If the Biba label is to be changed, authorize as appropriate.
	 */
	if (new->mb_flags & MAC_BIBA_FLAG_EFFECTIVE) {
		/*
		 * To relabel a socket, the new socket effective must be in
		 * the subject range.
		 */
		if (!mac_biba_effective_in_range(new, subj))
			return (EPERM);

		/*
		 * To change the Biba label on the socket to contain EQUAL,
		 * the subject must have appropriate privilege.
		 */
		if (mac_biba_contains_equal(new)) {
			error = mac_biba_subject_privileged(subj);
			if (error)
				return (error);
		}
	}

	return (0);
}

static int
mac_biba_socket_check_visible(struct ucred *cred, struct socket *so,
    struct label *solabel)
{
	struct mac_biba *subj, *obj;

	if (!mac_biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(solabel);

	if (!mac_biba_dominate_effective(obj, subj))
		return (ENOENT);

	return (0);
}

/*
 * Some system privileges are allowed regardless of integrity grade; others
 * are allowed only when running with privilege with respect to the Biba
 * policy as they might otherwise allow bypassing of the integrity policy.
 */
static int
mac_biba_priv_check(struct ucred *cred, int priv)
{
	struct mac_biba *subj;
	int error;

	if (!mac_biba_enabled)
		return (0);

	/*
	 * Exempt only specific privileges from the Biba integrity policy.
	 */
	switch (priv) {
	case PRIV_KTRACE:
	case PRIV_MSGBUF:

	/*
	 * Allow processes to manipulate basic process audit properties, and
	 * to submit audit records.
	 */
	case PRIV_AUDIT_GETAUDIT:
	case PRIV_AUDIT_SETAUDIT:
	case PRIV_AUDIT_SUBMIT:

	/*
	 * Allow processes to manipulate their regular UNIX credentials.
	 */
	case PRIV_CRED_SETUID:
	case PRIV_CRED_SETEUID:
	case PRIV_CRED_SETGID:
	case PRIV_CRED_SETEGID:
	case PRIV_CRED_SETGROUPS:
	case PRIV_CRED_SETREUID:
	case PRIV_CRED_SETREGID:
	case PRIV_CRED_SETRESUID:
	case PRIV_CRED_SETRESGID:

	/*
	 * Allow processes to perform system monitoring.
	 */
	case PRIV_SEEOTHERGIDS:
	case PRIV_SEEOTHERUIDS:
		break;

	/*
	 * Allow access to general process debugging facilities.  We
	 * separately control debugging based on MAC label.
	 */
	case PRIV_DEBUG_DIFFCRED:
	case PRIV_DEBUG_SUGID:
	case PRIV_DEBUG_UNPRIV:

	/*
	 * Allow manipulating jails.
	 */
	case PRIV_JAIL_ATTACH:

	/*
	 * Allow privilege with respect to the Partition policy, but not the
	 * Privs policy.
	 */
	case PRIV_MAC_PARTITION:

	/*
	 * Allow privilege with respect to process resource limits and login
	 * context.
	 */
	case PRIV_PROC_LIMIT:
	case PRIV_PROC_SETLOGIN:
	case PRIV_PROC_SETRLIMIT:

	/*
	 * Allow System V and POSIX IPC privileges.
	 */
	case PRIV_IPC_READ:
	case PRIV_IPC_WRITE:
	case PRIV_IPC_ADMIN:
	case PRIV_IPC_MSGSIZE:
	case PRIV_MQ_ADMIN:

	/*
	 * Allow certain scheduler manipulations -- possibly this should be
	 * controlled by more fine-grained policy, as potentially low
	 * integrity processes can deny CPU to higher integrity ones.
	 */
	case PRIV_SCHED_DIFFCRED:
	case PRIV_SCHED_SETPRIORITY:
	case PRIV_SCHED_RTPRIO:
	case PRIV_SCHED_SETPOLICY:
	case PRIV_SCHED_SET:
	case PRIV_SCHED_SETPARAM:

	/*
	 * More IPC privileges.
	 */
	case PRIV_SEM_WRITE:

	/*
	 * Allow signaling privileges subject to integrity policy.
	 */
	case PRIV_SIGNAL_DIFFCRED:
	case PRIV_SIGNAL_SUGID:

	/*
	 * Allow access to only limited sysctls from lower integrity levels;
	 * piggy-back on the Jail definition.
	 */
	case PRIV_SYSCTL_WRITEJAIL:

	/*
	 * Allow TTY-based privileges, subject to general device access using
	 * labels on TTY device nodes, but not console privilege.
	 */
	case PRIV_TTY_DRAINWAIT:
	case PRIV_TTY_DTRWAIT:
	case PRIV_TTY_EXCLUSIVE:
	case PRIV_TTY_PRISON:
	case PRIV_TTY_STI:
	case PRIV_TTY_SETA:

	/*
	 * Grant most VFS privileges, as almost all are in practice bounded
	 * by more specific checks using labels.
	 */
	case PRIV_VFS_READ:
	case PRIV_VFS_WRITE:
	case PRIV_VFS_ADMIN:
	case PRIV_VFS_EXEC:
	case PRIV_VFS_LOOKUP:
	case PRIV_VFS_CHFLAGS_DEV:
	case PRIV_VFS_CHOWN:
	case PRIV_VFS_CHROOT:
	case PRIV_VFS_RETAINSUGID:
	case PRIV_VFS_EXCEEDQUOTA:
	case PRIV_VFS_FCHROOT:
	case PRIV_VFS_FHOPEN:
	case PRIV_VFS_FHSTATFS:
	case PRIV_VFS_GENERATION:
	case PRIV_VFS_GETFH:
	case PRIV_VFS_GETQUOTA:
	case PRIV_VFS_LINK:
	case PRIV_VFS_MOUNT:
	case PRIV_VFS_MOUNT_OWNER:
	case PRIV_VFS_MOUNT_PERM:
	case PRIV_VFS_MOUNT_SUIDDIR:
	case PRIV_VFS_MOUNT_NONUSER:
	case PRIV_VFS_SETGID:
	case PRIV_VFS_STICKYFILE:
	case PRIV_VFS_SYSFLAGS:
	case PRIV_VFS_UNMOUNT:

	/*
	 * Allow VM privileges; it would be nice if these were subject to
	 * resource limits.
	 */
	case PRIV_VM_MADV_PROTECT:
	case PRIV_VM_MLOCK:
	case PRIV_VM_MUNLOCK:

	/*
	 * Allow some but not all network privileges.  In general, dont allow
	 * reconfiguring the network stack, just normal use.
	 */
	case PRIV_NETATALK_RESERVEDPORT:
	case PRIV_NETINET_RESERVEDPORT:
	case PRIV_NETINET_RAW:
	case PRIV_NETINET_REUSEPORT:
	case PRIV_NETIPX_RESERVEDPORT:
	case PRIV_NETIPX_RAW:
		break;

	/*
	 * All remaining system privileges are allow only if the process
	 * holds privilege with respect to the Biba policy.
	 */
	default:
		subj = SLOT(cred->cr_label);
		error = mac_biba_subject_privileged(subj);
		if (error)
			return (error);
	}
	return (0);
}

static int
mac_biba_system_check_acct(struct ucred *cred, struct vnode *vp,
    struct label *vplabel)
{
	struct mac_biba *subj, *obj;
	int error;

	if (!mac_biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);

	error = mac_biba_subject_privileged(subj);
	if (error)
		return (error);

	if (vplabel == NULL)
		return (0);

	obj = SLOT(vplabel);
	if (!mac_biba_high_effective(obj))
		return (EACCES);

	return (0);
}

static int
mac_biba_system_check_auditctl(struct ucred *cred, struct vnode *vp,
    struct label *vplabel)
{
	struct mac_biba *subj, *obj;
	int error;

	if (!mac_biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);

	error = mac_biba_subject_privileged(subj);
	if (error)
		return (error);

	if (vplabel == NULL)
		return (0);

	obj = SLOT(vplabel);
	if (!mac_biba_high_effective(obj))
		return (EACCES);

	return (0);
}

static int
mac_biba_system_check_auditon(struct ucred *cred, int cmd)
{
	struct mac_biba *subj;
	int error;

	if (!mac_biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);

	error = mac_biba_subject_privileged(subj);
	if (error)
		return (error);

	return (0);
}

static int
mac_biba_system_check_swapon(struct ucred *cred, struct vnode *vp,
    struct label *vplabel)
{
	struct mac_biba *subj, *obj;
	int error;

	if (!mac_biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	error = mac_biba_subject_privileged(subj);
	if (error)
		return (error);

	if (!mac_biba_high_effective(obj))
		return (EACCES);

	return (0);
}

static int
mac_biba_system_check_swapoff(struct ucred *cred, struct vnode *vp,
    struct label *label)
{
	struct mac_biba *subj;
	int error;

	if (!mac_biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);

	error = mac_biba_subject_privileged(subj);
	if (error)
		return (error);

	return (0);
}

static int
mac_biba_system_check_sysctl(struct ucred *cred, struct sysctl_oid *oidp,
    void *arg1, int arg2, struct sysctl_req *req)
{
	struct mac_biba *subj;
	int error;

	if (!mac_biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);

	/*
	 * Treat sysctl variables without CTLFLAG_ANYBODY flag as
	 * biba/high, but also require privilege to change them.
	 */
	if (req->newptr != NULL && (oidp->oid_kind & CTLFLAG_ANYBODY) == 0) {
		if (!mac_biba_subject_dominate_high(subj))
			return (EACCES);

		error = mac_biba_subject_privileged(subj);
		if (error)
			return (error);
	}

	return (0);
}

static int
mac_biba_vnode_check_chdir(struct ucred *cred, struct vnode *dvp,
    struct label *dvplabel)
{
	struct mac_biba *subj, *obj;

	if (!mac_biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(dvplabel);

	if (!mac_biba_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
mac_biba_vnode_check_chroot(struct ucred *cred, struct vnode *dvp,
    struct label *dvplabel)
{
	struct mac_biba *subj, *obj;

	if (!mac_biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(dvplabel);

	if (!mac_biba_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
mac_biba_vnode_check_create(struct ucred *cred, struct vnode *dvp,
    struct label *dvplabel, struct componentname *cnp, struct vattr *vap)
{
	struct mac_biba *subj, *obj;

	if (!mac_biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(dvplabel);

	if (!mac_biba_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
mac_biba_vnode_check_deleteacl(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, acl_type_t type)
{
	struct mac_biba *subj, *obj;

	if (!mac_biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (!mac_biba_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
mac_biba_vnode_check_deleteextattr(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, int attrnamespace, const char *name)
{
	struct mac_biba *subj, *obj;

	if (!mac_biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (!mac_biba_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
mac_biba_vnode_check_exec(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, struct image_params *imgp,
    struct label *execlabel)
{
	struct mac_biba *subj, *obj, *exec;
	int error;

	if (execlabel != NULL) {
		/*
		 * We currently don't permit labels to be changed at
		 * exec-time as part of Biba, so disallow non-NULL
		 * Biba label elements in the execlabel.
		 */
		exec = SLOT(execlabel);
		error = biba_atmostflags(exec, 0);
		if (error)
			return (error);
	}

	if (!mac_biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (!mac_biba_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
mac_biba_vnode_check_getacl(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, acl_type_t type)
{
	struct mac_biba *subj, *obj;

	if (!mac_biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (!mac_biba_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
mac_biba_vnode_check_getextattr(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, int attrnamespace, const char *name,
    struct uio *uio)
{
	struct mac_biba *subj, *obj;

	if (!mac_biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (!mac_biba_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
mac_biba_vnode_check_link(struct ucred *cred, struct vnode *dvp,
    struct label *dvplabel, struct vnode *vp, struct label *vplabel,
    struct componentname *cnp)
{
	struct mac_biba *subj, *obj;

	if (!mac_biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(dvplabel);

	if (!mac_biba_dominate_effective(subj, obj))
		return (EACCES);

	obj = SLOT(vplabel);

	if (!mac_biba_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
mac_biba_vnode_check_listextattr(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, int attrnamespace)
{
	struct mac_biba *subj, *obj;

	if (!mac_biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (!mac_biba_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
mac_biba_vnode_check_lookup(struct ucred *cred, struct vnode *dvp,
    struct label *dvplabel, struct componentname *cnp)
{
	struct mac_biba *subj, *obj;

	if (!mac_biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(dvplabel);

	if (!mac_biba_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
mac_biba_vnode_check_mmap(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, int prot, int flags)
{
	struct mac_biba *subj, *obj;

	/*
	 * Rely on the use of open()-time protections to handle
	 * non-revocation cases.
	 */
	if (!mac_biba_enabled || !revocation_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (prot & (VM_PROT_READ | VM_PROT_EXECUTE)) {
		if (!mac_biba_dominate_effective(obj, subj))
			return (EACCES);
	}
	if (((prot & VM_PROT_WRITE) != 0) && ((flags & MAP_SHARED) != 0)) {
		if (!mac_biba_dominate_effective(subj, obj))
			return (EACCES);
	}

	return (0);
}

static int
mac_biba_vnode_check_open(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, int acc_mode)
{
	struct mac_biba *subj, *obj;

	if (!mac_biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	/* XXX privilege override for admin? */
	if (acc_mode & (VREAD | VEXEC | VSTAT)) {
		if (!mac_biba_dominate_effective(obj, subj))
			return (EACCES);
	}
	if (acc_mode & (VWRITE | VAPPEND | VADMIN)) {
		if (!mac_biba_dominate_effective(subj, obj))
			return (EACCES);
	}

	return (0);
}

static int
mac_biba_vnode_check_poll(struct ucred *active_cred, struct ucred *file_cred,
    struct vnode *vp, struct label *vplabel)
{
	struct mac_biba *subj, *obj;

	if (!mac_biba_enabled || !revocation_enabled)
		return (0);

	subj = SLOT(active_cred->cr_label);
	obj = SLOT(vplabel);

	if (!mac_biba_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
mac_biba_vnode_check_read(struct ucred *active_cred, struct ucred *file_cred,
    struct vnode *vp, struct label *vplabel)
{
	struct mac_biba *subj, *obj;

	if (!mac_biba_enabled || !revocation_enabled)
		return (0);

	subj = SLOT(active_cred->cr_label);
	obj = SLOT(vplabel);

	if (!mac_biba_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
mac_biba_vnode_check_readdir(struct ucred *cred, struct vnode *dvp,
    struct label *dvplabel)
{
	struct mac_biba *subj, *obj;

	if (!mac_biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(dvplabel);

	if (!mac_biba_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
mac_biba_vnode_check_readlink(struct ucred *cred, struct vnode *vp,
    struct label *vplabel)
{
	struct mac_biba *subj, *obj;

	if (!mac_biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (!mac_biba_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
mac_biba_vnode_check_relabel(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, struct label *newlabel)
{
	struct mac_biba *old, *new, *subj;
	int error;

	old = SLOT(vplabel);
	new = SLOT(newlabel);
	subj = SLOT(cred->cr_label);

	/*
	 * If there is a Biba label update for the vnode, it must be a
	 * effective label.
	 */
	error = biba_atmostflags(new, MAC_BIBA_FLAG_EFFECTIVE);
	if (error)
		return (error);

	/*
	 * To perform a relabel of the vnode (Biba label or not), Biba must
	 * authorize the relabel.
	 */
	if (!mac_biba_effective_in_range(old, subj))
		return (EPERM);

	/*
	 * If the Biba label is to be changed, authorize as appropriate.
	 */
	if (new->mb_flags & MAC_BIBA_FLAG_EFFECTIVE) {
		/*
		 * To change the Biba label on a vnode, the new vnode label
		 * must be in the subject range.
		 */
		if (!mac_biba_effective_in_range(new, subj))
			return (EPERM);

		/*
		 * To change the Biba label on the vnode to be EQUAL,
		 * the subject must have appropriate privilege.
		 */
		if (mac_biba_contains_equal(new)) {
			error = mac_biba_subject_privileged(subj);
			if (error)
				return (error);
		}
	}

	return (0);
}

static int
mac_biba_vnode_check_rename_from(struct ucred *cred, struct vnode *dvp,
    struct label *dvplabel, struct vnode *vp, struct label *vplabel,
    struct componentname *cnp)
{
	struct mac_biba *subj, *obj;

	if (!mac_biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(dvplabel);

	if (!mac_biba_dominate_effective(subj, obj))
		return (EACCES);

	obj = SLOT(vplabel);

	if (!mac_biba_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
mac_biba_vnode_check_rename_to(struct ucred *cred, struct vnode *dvp,
    struct label *dvplabel, struct vnode *vp, struct label *vplabel,
    int samedir, struct componentname *cnp)
{
	struct mac_biba *subj, *obj;

	if (!mac_biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(dvplabel);

	if (!mac_biba_dominate_effective(subj, obj))
		return (EACCES);

	if (vp != NULL) {
		obj = SLOT(vplabel);

		if (!mac_biba_dominate_effective(subj, obj))
			return (EACCES);
	}

	return (0);
}

static int
mac_biba_vnode_check_revoke(struct ucred *cred, struct vnode *vp,
    struct label *vplabel)
{
	struct mac_biba *subj, *obj;

	if (!mac_biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (!mac_biba_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
mac_biba_vnode_check_setacl(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, acl_type_t type, struct acl *acl)
{
	struct mac_biba *subj, *obj;

	if (!mac_biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (!mac_biba_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
mac_biba_vnode_check_setextattr(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, int attrnamespace, const char *name,
    struct uio *uio)
{
	struct mac_biba *subj, *obj;

	if (!mac_biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (!mac_biba_dominate_effective(subj, obj))
		return (EACCES);

	/* XXX: protect the MAC EA in a special way? */

	return (0);
}

static int
mac_biba_vnode_check_setflags(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, u_long flags)
{
	struct mac_biba *subj, *obj;

	if (!mac_biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (!mac_biba_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
mac_biba_vnode_check_setmode(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, mode_t mode)
{
	struct mac_biba *subj, *obj;

	if (!mac_biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (!mac_biba_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
mac_biba_vnode_check_setowner(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, uid_t uid, gid_t gid)
{
	struct mac_biba *subj, *obj;

	if (!mac_biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (!mac_biba_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
mac_biba_vnode_check_setutimes(struct ucred *cred, struct vnode *vp,
    struct label *vplabel, struct timespec atime, struct timespec mtime)
{
	struct mac_biba *subj, *obj;

	if (!mac_biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(vplabel);

	if (!mac_biba_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
mac_biba_vnode_check_stat(struct ucred *active_cred, struct ucred *file_cred,
    struct vnode *vp, struct label *vplabel)
{
	struct mac_biba *subj, *obj;

	if (!mac_biba_enabled)
		return (0);

	subj = SLOT(active_cred->cr_label);
	obj = SLOT(vplabel);

	if (!mac_biba_dominate_effective(obj, subj))
		return (EACCES);

	return (0);
}

static int
mac_biba_vnode_check_unlink(struct ucred *cred, struct vnode *dvp,
    struct label *dvplabel, struct vnode *vp, struct label *vplabel,
    struct componentname *cnp)
{
	struct mac_biba *subj, *obj;

	if (!mac_biba_enabled)
		return (0);

	subj = SLOT(cred->cr_label);
	obj = SLOT(dvplabel);

	if (!mac_biba_dominate_effective(subj, obj))
		return (EACCES);

	obj = SLOT(vplabel);

	if (!mac_biba_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static int
mac_biba_vnode_check_write(struct ucred *active_cred,
    struct ucred *file_cred, struct vnode *vp, struct label *vplabel)
{
	struct mac_biba *subj, *obj;

	if (!mac_biba_enabled || !revocation_enabled)
		return (0);

	subj = SLOT(active_cred->cr_label);
	obj = SLOT(vplabel);

	if (!mac_biba_dominate_effective(subj, obj))
		return (EACCES);

	return (0);
}

static void
mac_biba_associate_nfsd_label(struct ucred *cred)
{
	struct mac_biba *label;

	label = SLOT(cred->cr_label);
	mac_biba_set_effective(label, MAC_BIBA_TYPE_LOW, 0, NULL);
	mac_biba_set_range(label, MAC_BIBA_TYPE_LOW, 0, NULL,
	    MAC_BIBA_TYPE_HIGH, 0, NULL);
}

static void
mac_biba_init_syncache_from_inpcb(struct label *label, struct inpcb *inp)
{
	struct mac_biba *source, *dest;

	source = SLOT(inp->inp_label);
	dest = SLOT(label);
	mac_biba_copy_effective(source, dest);
}

static void
mac_biba_create_mbuf_from_syncache(struct label *sc_label, struct mbuf *m,
    struct label *mlabel)
{
	struct mac_biba *source, *dest;

	source = SLOT(sc_label);
	dest = SLOT(mlabel);
	mac_biba_copy_effective(source, dest);
}

static struct mac_policy_ops mac_biba_ops =
{
	.mpo_init = mac_biba_init,
	.mpo_bpfdesc_init_label = mac_biba_init_label,
	.mpo_cred_init_label = mac_biba_init_label,
	.mpo_devfs_init_label = mac_biba_init_label,
	.mpo_ifnet_init_label = mac_biba_init_label,
	.mpo_inpcb_init_label = mac_biba_init_label_waitcheck,
	.mpo_init_syncache_label = mac_biba_init_label_waitcheck,
	.mpo_sysvmsg_init_label = mac_biba_init_label,
	.mpo_sysvmsq_init_label = mac_biba_init_label,
	.mpo_sysvsem_init_label = mac_biba_init_label,
	.mpo_sysvshm_init_label = mac_biba_init_label,
	.mpo_ipq_init_label = mac_biba_init_label_waitcheck,
	.mpo_mbuf_init_label = mac_biba_init_label_waitcheck,
	.mpo_mount_init_label = mac_biba_init_label,
	.mpo_pipe_init_label = mac_biba_init_label,
	.mpo_posixsem_init_label = mac_biba_init_label,
	.mpo_socket_init_label = mac_biba_init_label_waitcheck,
	.mpo_socketpeer_init_label = mac_biba_init_label_waitcheck,
	.mpo_init_syncache_from_inpcb = mac_biba_init_syncache_from_inpcb,
	.mpo_vnode_init_label = mac_biba_init_label,
	.mpo_bpfdesc_destroy_label = mac_biba_destroy_label,
	.mpo_cred_destroy_label = mac_biba_destroy_label,
	.mpo_devfs_destroy_label = mac_biba_destroy_label,
	.mpo_ifnet_destroy_label = mac_biba_destroy_label,
	.mpo_inpcb_destroy_label = mac_biba_destroy_label,
	.mpo_destroy_syncache_label = mac_biba_destroy_label,
	.mpo_sysvmsg_destroy_label = mac_biba_destroy_label,
	.mpo_sysvmsq_destroy_label = mac_biba_destroy_label,
	.mpo_sysvsem_destroy_label = mac_biba_destroy_label,
	.mpo_sysvshm_destroy_label = mac_biba_destroy_label,
	.mpo_ipq_destroy_label = mac_biba_destroy_label,
	.mpo_mbuf_destroy_label = mac_biba_destroy_label,
	.mpo_mount_destroy_label = mac_biba_destroy_label,
	.mpo_pipe_destroy_label = mac_biba_destroy_label,
	.mpo_posixsem_destroy_label = mac_biba_destroy_label,
	.mpo_socket_destroy_label = mac_biba_destroy_label,
	.mpo_socketpeer_destroy_label = mac_biba_destroy_label,
	.mpo_vnode_destroy_label = mac_biba_destroy_label,
	.mpo_cred_copy_label = mac_biba_copy_label,
	.mpo_ifnet_copy_label = mac_biba_copy_label,
	.mpo_mbuf_copy_label = mac_biba_copy_label,
	.mpo_pipe_copy_label = mac_biba_copy_label,
	.mpo_socket_copy_label = mac_biba_copy_label,
	.mpo_vnode_copy_label = mac_biba_copy_label,
	.mpo_cred_externalize_label = mac_biba_externalize_label,
	.mpo_ifnet_externalize_label = mac_biba_externalize_label,
	.mpo_pipe_externalize_label = mac_biba_externalize_label,
	.mpo_socket_externalize_label = mac_biba_externalize_label,
	.mpo_socketpeer_externalize_label = mac_biba_externalize_label,
	.mpo_vnode_externalize_label = mac_biba_externalize_label,
	.mpo_cred_internalize_label = mac_biba_internalize_label,
	.mpo_ifnet_internalize_label = mac_biba_internalize_label,
	.mpo_pipe_internalize_label = mac_biba_internalize_label,
	.mpo_socket_internalize_label = mac_biba_internalize_label,
	.mpo_vnode_internalize_label = mac_biba_internalize_label,
	.mpo_devfs_create_device = mac_biba_devfs_create_device,
	.mpo_devfs_create_directory = mac_biba_devfs_create_directory,
	.mpo_devfs_create_symlink = mac_biba_devfs_create_symlink,
	.mpo_mount_create = mac_biba_mount_create,
	.mpo_vnode_relabel = mac_biba_vnode_relabel,
	.mpo_devfs_update = mac_biba_devfs_update,
	.mpo_devfs_vnode_associate = mac_biba_devfs_vnode_associate,
	.mpo_vnode_associate_extattr = mac_biba_vnode_associate_extattr,
	.mpo_vnode_associate_singlelabel = mac_biba_vnode_associate_singlelabel,
	.mpo_vnode_create_extattr = mac_biba_vnode_create_extattr,
	.mpo_vnode_setlabel_extattr = mac_biba_vnode_setlabel_extattr,
	.mpo_socket_create_mbuf = mac_biba_socket_create_mbuf,
	.mpo_create_mbuf_from_syncache = mac_biba_create_mbuf_from_syncache,
	.mpo_pipe_create = mac_biba_pipe_create,
	.mpo_posixsem_create = mac_biba_posixsem_create,
	.mpo_socket_create = mac_biba_socket_create,
	.mpo_socket_newconn = mac_biba_socket_newconn,
	.mpo_pipe_relabel = mac_biba_pipe_relabel,
	.mpo_socket_relabel = mac_biba_socket_relabel,
	.mpo_socketpeer_set_from_mbuf = mac_biba_socketpeer_set_from_mbuf,
	.mpo_socketpeer_set_from_socket = mac_biba_socketpeer_set_from_socket,
	.mpo_bpfdesc_create = mac_biba_bpfdesc_create,
	.mpo_ipq_reassemble = mac_biba_ipq_reassemble,
	.mpo_netinet_fragment = mac_biba_netinet_fragment,
	.mpo_ifnet_create = mac_biba_ifnet_create,
	.mpo_inpcb_create = mac_biba_inpcb_create,
	.mpo_sysvmsg_create = mac_biba_sysvmsg_create,
	.mpo_sysvmsq_create = mac_biba_sysvmsq_create,
	.mpo_sysvsem_create = mac_biba_sysvsem_create,
	.mpo_sysvshm_create = mac_biba_sysvshm_create,
	.mpo_ipq_create = mac_biba_ipq_create,
	.mpo_inpcb_create_mbuf = mac_biba_inpcb_create_mbuf,
	.mpo_create_mbuf_linklayer = mac_biba_create_mbuf_linklayer,
	.mpo_bpfdesc_create_mbuf = mac_biba_bpfdesc_create_mbuf,
	.mpo_ifnet_create_mbuf = mac_biba_ifnet_create_mbuf,
	.mpo_mbuf_create_multicast_encap = mac_biba_mbuf_create_multicast_encap,
	.mpo_mbuf_create_netlayer = mac_biba_mbuf_create_netlayer,
	.mpo_ipq_match = mac_biba_ipq_match,
	.mpo_ifnet_relabel = mac_biba_ifnet_relabel,
	.mpo_ipq_update = mac_biba_ipq_update,
	.mpo_inpcb_sosetlabel = mac_biba_inpcb_sosetlabel,
	.mpo_proc_create_swapper = mac_biba_proc_create_swapper,
	.mpo_proc_create_init = mac_biba_proc_create_init,
	.mpo_cred_relabel = mac_biba_cred_relabel,
	.mpo_sysvmsg_cleanup = mac_biba_sysvmsg_cleanup,
	.mpo_sysvmsq_cleanup = mac_biba_sysvmsq_cleanup,
	.mpo_sysvsem_cleanup = mac_biba_sysvsem_cleanup,
	.mpo_sysvshm_cleanup = mac_biba_sysvshm_cleanup,
	.mpo_bpfdesc_check_receive = mac_biba_bpfdesc_check_receive,
	.mpo_cred_check_relabel = mac_biba_cred_check_relabel,
	.mpo_cred_check_visible = mac_biba_cred_check_visible,
	.mpo_ifnet_check_relabel = mac_biba_ifnet_check_relabel,
	.mpo_ifnet_check_transmit = mac_biba_ifnet_check_transmit,
	.mpo_inpcb_check_deliver = mac_biba_inpcb_check_deliver,
	.mpo_sysvmsq_check_msgrcv = mac_biba_sysvmsq_check_msgrcv,
	.mpo_sysvmsq_check_msgrmid = mac_biba_sysvmsq_check_msgrmid,
	.mpo_sysvmsq_check_msqget = mac_biba_sysvmsq_check_msqget,
	.mpo_sysvmsq_check_msqsnd = mac_biba_sysvmsq_check_msqsnd,
	.mpo_sysvmsq_check_msqrcv = mac_biba_sysvmsq_check_msqrcv,
	.mpo_sysvmsq_check_msqctl = mac_biba_sysvmsq_check_msqctl,
	.mpo_sysvsem_check_semctl = mac_biba_sysvsem_check_semctl,
	.mpo_sysvsem_check_semget = mac_biba_sysvsem_check_semget,
	.mpo_sysvsem_check_semop = mac_biba_sysvsem_check_semop,
	.mpo_sysvshm_check_shmat = mac_biba_sysvshm_check_shmat,
	.mpo_sysvshm_check_shmctl = mac_biba_sysvshm_check_shmctl,
	.mpo_sysvshm_check_shmget = mac_biba_sysvshm_check_shmget,
	.mpo_kld_check_load = mac_biba_kld_check_load,
	.mpo_mount_check_stat = mac_biba_mount_check_stat,
	.mpo_pipe_check_ioctl = mac_biba_pipe_check_ioctl,
	.mpo_pipe_check_poll = mac_biba_pipe_check_poll,
	.mpo_pipe_check_read = mac_biba_pipe_check_read,
	.mpo_pipe_check_relabel = mac_biba_pipe_check_relabel,
	.mpo_pipe_check_stat = mac_biba_pipe_check_stat,
	.mpo_pipe_check_write = mac_biba_pipe_check_write,
	.mpo_posixsem_check_destroy = mac_biba_posixsem_check_write,
	.mpo_posixsem_check_getvalue = mac_biba_posixsem_check_rdonly,
	.mpo_posixsem_check_open = mac_biba_posixsem_check_write,
	.mpo_posixsem_check_post = mac_biba_posixsem_check_write,
	.mpo_posixsem_check_unlink = mac_biba_posixsem_check_write,
	.mpo_posixsem_check_wait = mac_biba_posixsem_check_write,
	.mpo_proc_check_debug = mac_biba_proc_check_debug,
	.mpo_proc_check_sched = mac_biba_proc_check_sched,
	.mpo_proc_check_signal = mac_biba_proc_check_signal,
	.mpo_socket_check_deliver = mac_biba_socket_check_deliver,
	.mpo_socket_check_relabel = mac_biba_socket_check_relabel,
	.mpo_socket_check_visible = mac_biba_socket_check_visible,
	.mpo_system_check_acct = mac_biba_system_check_acct,
	.mpo_system_check_auditctl = mac_biba_system_check_auditctl,
	.mpo_system_check_auditon = mac_biba_system_check_auditon,
	.mpo_system_check_swapon = mac_biba_system_check_swapon,
	.mpo_system_check_swapoff = mac_biba_system_check_swapoff,
	.mpo_system_check_sysctl = mac_biba_system_check_sysctl,
	.mpo_vnode_check_access = mac_biba_vnode_check_open,
	.mpo_vnode_check_chdir = mac_biba_vnode_check_chdir,
	.mpo_vnode_check_chroot = mac_biba_vnode_check_chroot,
	.mpo_vnode_check_create = mac_biba_vnode_check_create,
	.mpo_vnode_check_deleteacl = mac_biba_vnode_check_deleteacl,
	.mpo_vnode_check_deleteextattr = mac_biba_vnode_check_deleteextattr,
	.mpo_vnode_check_exec = mac_biba_vnode_check_exec,
	.mpo_vnode_check_getacl = mac_biba_vnode_check_getacl,
	.mpo_vnode_check_getextattr = mac_biba_vnode_check_getextattr,
	.mpo_vnode_check_link = mac_biba_vnode_check_link,
	.mpo_vnode_check_listextattr = mac_biba_vnode_check_listextattr,
	.mpo_vnode_check_lookup = mac_biba_vnode_check_lookup,
	.mpo_vnode_check_mmap = mac_biba_vnode_check_mmap,
	.mpo_vnode_check_open = mac_biba_vnode_check_open,
	.mpo_vnode_check_poll = mac_biba_vnode_check_poll,
	.mpo_vnode_check_read = mac_biba_vnode_check_read,
	.mpo_vnode_check_readdir = mac_biba_vnode_check_readdir,
	.mpo_vnode_check_readlink = mac_biba_vnode_check_readlink,
	.mpo_vnode_check_relabel = mac_biba_vnode_check_relabel,
	.mpo_vnode_check_rename_from = mac_biba_vnode_check_rename_from,
	.mpo_vnode_check_rename_to = mac_biba_vnode_check_rename_to,
	.mpo_vnode_check_revoke = mac_biba_vnode_check_revoke,
	.mpo_vnode_check_setacl = mac_biba_vnode_check_setacl,
	.mpo_vnode_check_setextattr = mac_biba_vnode_check_setextattr,
	.mpo_vnode_check_setflags = mac_biba_vnode_check_setflags,
	.mpo_vnode_check_setmode = mac_biba_vnode_check_setmode,
	.mpo_vnode_check_setowner = mac_biba_vnode_check_setowner,
	.mpo_vnode_check_setutimes = mac_biba_vnode_check_setutimes,
	.mpo_vnode_check_stat = mac_biba_vnode_check_stat,
	.mpo_vnode_check_unlink = mac_biba_vnode_check_unlink,
	.mpo_vnode_check_write = mac_biba_vnode_check_write,
	.mpo_associate_nfsd_label = mac_biba_associate_nfsd_label,
	.mpo_mbuf_create_from_firewall = mac_biba_mbuf_create_from_firewall,
	.mpo_priv_check = mac_biba_priv_check,
};

MAC_POLICY_SET(&mac_biba_ops, mac_biba, "TrustedBSD MAC/Biba",
    MPC_LOADTIME_FLAG_NOTLATE | MPC_LOADTIME_FLAG_LABELMBUFS, &mac_biba_slot);
