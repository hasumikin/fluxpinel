/* kernel_prelude.h -- the start of the TU of the host build, before the
   kernel (docs/design.md, section 3.6).

   The runtime names a class value through sp_class_to_s, which the kernel
   defines for its own classes only. This file includes spinel_rt.h before
   the kernel does, under another name for that function, so that the
   runtime calls fx_class_to_s_any (fx_tu.c). The kernel then defines
   sp_class_to_s, and fx_class_to_s_any calls it for a class of the
   kernel.

   The defines are those that the kernel gives before its include of
   spinel_rt.h. The Makefile checks that the two agree. */

#define SPINEL_EXT_KERNEL 1
#define SP_SHARE_STRING_LITERALS 1
#define SP_SHARE_BOX_HANDLES 1
#define sp_class_to_s fx_class_to_s_any
#include "spinel_rt.h"
#undef sp_class_to_s
