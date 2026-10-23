/* main.c -- the Fluxpinel command and the root of the one TU.

   Usage: fluxpinel FILE.rb [ARG...]
          fluxpinel --console    a console on stdin (console.c)

   A host build with FX_KERNEL_MAIN (an entry such as "Logq.main") takes
   no FILE.rb: it calls the entry with ARGV, which holds every argument.

   spinel_rt.h declares hooks as static, so all Fluxpinel code that uses the
   runtime is one TU. This file includes the others in order. */

#include "fx_tu.c"
#include "bop_gen_out.c"
#include "fluxpinel.h"
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
#include "process.c"
#include "file.c"
#include "host.c"
#include "console.c"

static int fx_console_mode;   /* fluxpinel --console */

static void fx_body(void) {
  SP_GC_SAVE();
  fx_tu_init();
  fx_bop_init();
  L.ctl_val = sp_box_nil();
  _sp_gc_root_push((void **)((uintptr_t)&L.ctl_val | (uintptr_t)1));
  fx_object_init();
  fx_host_init();
  fx_hash_init();
  fx_array_init();
  fx_exc_hooks();
  fx_re_init();
  fx_global_init();
  /* a fiber runs the walker, whose frames are larger than those of AOT
     code: the size that codegen asks for an unoptimised build */
  sp_fiber_stack_hint((size_t)1 << 20);
  /* The fewest workers, also for a thread of the runtime itself (see
     Thread.new in fiber.c). The threaded runtime needs its root fiber
     before any fiber runs. */
  setenv("SPINEL_WORKERS", "1", 1);
  fx_sched_start();
  fx_fs_init();

#ifdef FX_KERNEL_MAIN
  const char *src = FX_KERNEL_MAIN "(ARGV)";
  fx_load_main_src(L.path, L.path, strdup(src), strlen(src));
#else
  if (fx_console_mode) {
    /* an empty main program starts the constant pool of the sources */
    fx_load_main_src(L.path, L.path, strdup(""), 0);
    fx_console_stdin();
  } else {
    fx_load_main(L.path);
  }
#endif
  /* a break of a Proc whose call has returned reaches the top */
  if (L.ctl == FX_CTL_BREAK_BLOCK) fx_gap("break from a block whose call has returned");
  fx_sched_end();
  sp_fin_run_exit();
}

int main(int argc, char **argv) {
#ifdef FX_KERNEL_MAIN
  L.path = argv[0];
  L.args = argv + 1;
  L.nargs = argc - 1;
#else
  if (argc < 2) { fprintf(stderr, "usage: fluxpinel FILE.rb [ARG...] | fluxpinel --console\n"); return 2; }
  fx_console_mode = !strcmp(argv[1], "--console");
  L.path = fx_console_mode ? "(console)" : argv[1];
  L.args = argv + 2;
  L.nargs = argc - 2;
#endif
  sp_main_stack_run(fx_body);
  return 0;
}
