#ifndef FX_TU_H
#define FX_TU_H

#define SP_SHARE_STRING_LITERALS 1
#define SP_SHARE_BOX_HANDLES 1
#include "spinel_rt.h"

/* Fluxpinel class ids start here, far above the ids that spinel codegen gives
   the classes of an AOT program (0, 1, ...). In the host build, an id from
   0 up to this one is a class of the kernel. */
#define FX_CLS_BASE 0x100000

/* The ids of the exception classes that only the runtime names (LoadError,
   Math::DomainError, ...) count down from here, below the builtin table. */
#define FX_EXC_CLS_BASE (-1000)

void fx_tu_init(void);

#endif
