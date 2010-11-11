/* ===-- negdi2.c - Implement __negdi2 -------------------------------------===
 *
 *                     The LLVM Compiler Infrastructure
 *
 * This file is distributed under the University of Illinois Open Source
 * License. See LICENSE.TXT for details.
 *
 * ===----------------------------------------------------------------------===
 *
 * This file implements __negdi2 for the compiler_rt library.
 *
 * ===----------------------------------------------------------------------===
 */

#include "int_lib.h"

/* Returns: -a */

di_int
__negdi2(di_int a)
{
    /* Note: this routine is here for API compatibility; any sane compiler
     * should expand it inline.
     */
    return -a;
}
