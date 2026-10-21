/* main.c -- the Lupe command and the root of the one TU.

   Usage: lupe FILE.rb

   spinel_rt.h declares hooks as static, so all Lupe code that uses the
   runtime is one TU. This file includes the others in order. */

#include "lupe_tu.c"
#include "bop_gen_out.c"
#include "lupe.h"
#include "object.c"
#include "walk.c"
#include "builtin.c"
#include "iter.c"
#include "hash.c"
#include "array.c"
#include "string.c"
#include "exc.c"
#include "re.c"
#include "global.c"
#include "struct.c"
#include "fiber.c"
#include "load.c"
#include "ffi.c"

static void lupe_body(void) {
  SP_GC_SAVE();
  lupe_tu_init();
  lupe_bop_init();
  L.ctl_val = sp_box_nil();
  _sp_gc_root_push((void **)((uintptr_t)&L.ctl_val | (uintptr_t)1));
  lupe_object_init();
  lupe_hash_init();
  lupe_array_init();
  lupe_exc_hooks();
  lupe_re_init();
  lupe_global_init();
  /* a fiber runs the walker, whose frames are larger than those of AOT
     code: the size that codegen asks for an unoptimised build */
  sp_fiber_stack_hint((size_t)1 << 20);
  /* The fewest workers, also for a thread of the runtime itself (see
     Thread.new in fiber.c). The threaded runtime needs its root fiber
     before any fiber runs. */
  setenv("SPINEL_WORKERS", "1", 1);
  lupe_sched_start();
  lupe_fs_init();

  lupe_load_main(L.path);
  /* a break of a Proc whose call has returned reaches the top */
  if (L.ctl == LUPE_CTL_BREAK_BLOCK) lupe_gap("break from a block whose call has returned");
  lupe_sched_end();
  sp_fin_run_exit();
}

int main(int argc, char **argv) {
  if (argc < 2) { fprintf(stderr, "usage: lupe FILE.rb [ARG...]\n"); return 2; }
  L.path = argv[1];
  L.args = argv + 2;
  L.nargs = argc - 2;
  sp_main_stack_run(lupe_body);
  return 0;
}
