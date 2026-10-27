/* fiber.c -- Fiber, Thread, Enumerator.new and the thread primitives.

   The runtime runs a fiber body on a C stack of its own, and a green
   thread or a generator Enumerator is a fiber too. Fluxpinel gives each of
   them one body, fx_fiber_body, which calls a block of the program on
   that stack. The fiber carries the block in its user_data (a
   FxFiberData), so the collector keeps it.

   Each fiber that runs walker code has a frame stack of its own
   (FxFrameStack, walk.c): fx_fiber_body makes it, and fx_fs finds
   it again after a switch. Everything else that a switch must swap, the
   GC roots and the rescue handlers, the runtime swaps itself. */

#include "fluxpinel.h"

#define FX_IS(s) (!strcmp(name, (s)))

enum { FX_FIB_FIBER, FX_FIB_THREAD, FX_FIB_GEN };

typedef struct {
  FxProc *proc;        /* the block that the fiber runs */
  sp_PolyArray *args;    /* a Thread: the arguments of Thread.new */
  int kind;
} FxFiberData;

static void fx_fiber_data_scan(void *p) {
  FxFiberData *d = (FxFiberData *)p;
  if (d->proc) sp_gc_mark(d->proc);
  if (d->args) sp_gc_mark(d->args);
}

/* The Proc that a fiber runs, from the block of the call. A break in it
   has no call to end. */
static FxProc *fx_fiber_proc(const FxBlock *blk) {
  FxBlock b = *blk;
  if (b.node && !b.proc) b.site = NULL;
  return (FxProc *)fx_block_proc(&b).v.p;
}

static FxFiberData *fx_fiber_data(const FxBlock *blk, int kind) {
  FxProc *p = fx_fiber_proc(blk);
  SP_GC_ROOT(p);
  FxFiberData *d = (FxFiberData *)sp_gc_alloc(sizeof *d, NULL, fx_fiber_data_scan);
  d->proc = p;
  d->args = NULL;
  d->kind = kind;
  return d;
}

/* The value that resume, Fiber.yield and the like pass: nil, the one
   argument, or an Array of them. */
static sp_RbVal fx_fiber_pack(int argc, sp_RbVal *argv) {
  if (argc == 0) return sp_box_nil();
  if (argc == 1) return argv[0];
  sp_PolyArray *a = sp_PolyArray_new();
  SP_GC_ROOT(a);
  for (int i = 0; i < argc; i++) sp_PolyArray_push(a, argv[i]);
  return sp_box_poly_array(a);
}

/* The body of every fiber that Fluxpinel starts. */
static void fx_fiber_body(sp_Fiber *f) {
  SP_GC_SAVE();
  FxFiberData *d = (FxFiberData *)f->user_data;
  SP_GC_ROOT(d);
  /* a Thread runs on a green thread of its own, which takes the walker
     lock; a Fiber or a generator runs on the thread of its resume, which
     holds it */
  if (d->kind == FX_FIB_THREAD) fx_lock();
  FxFrameStack *fs = fx_fs_new(f);
  fx_fs_put(f, fs);
  L.fs = fs;
  /* the arguments of the block: Thread.new's, the yielder of a generator,
     or what the first resume passed */
  sp_PolyArray *args = sp_PolyArray_new();
  SP_GC_ROOT(args);
  if (d->kind == FX_FIB_THREAD) {
    for (sp_int i = 0; d->args && i < d->args->len; i++) sp_PolyArray_push(args, d->args->data[i]);
  } else if (d->kind == FX_FIB_GEN) {
    sp_PolyArray_push(args, sp_box_obj(f, SP_BUILTIN_YIELDER));
  } else if (f->pass_argc > 1 && fx_is_array(f->resumed_value)) {
    for (sp_int i = 0; i < sp_poly_length(f->resumed_value); i++)
      sp_PolyArray_push(args, sp_poly_arr_get(f->resumed_value, i));
  } else if (f->pass_argc != 0) {
    sp_PolyArray_push(args, f->resumed_value);
  }
  sp_RbVal v = sp_box_nil();
  SP_GC_ROOT_RBVAL(v);
  /* a raise that leaves the body ends its frames, so a Proc made in them
     does not read the stack of a dead fiber */
  FX_TRY_PUSH();
  if (setjmp(sp_exc_stack[sp_exc_top - 1]) == 0) {
    v = fx_yield(&d->proc->b, (int)args->len, args->data);
    sp_exc_top--;
  } else {
    FX_TRY_LANDED(0);
    L.ctl = FX_CTL_NONE;
    FxCaught c = fx_caught();
    if (d->kind == FX_FIB_THREAD) fx_lock_set_depth(0);
    fx_reraise(c);
  }
  fs->depth = 0;
  FxCtl ctl = L.ctl;
  L.ctl = FX_CTL_NONE;
  if (d->kind == FX_FIB_THREAD) fx_lock_set_depth(0);
  if (ctl == FX_CTL_BREAK || ctl == FX_CTL_BREAK_BLOCK)
    sp_raise_cls("LocalJumpError", "break from proc-closure");
  if (ctl == FX_CTL_RETURN) sp_raise_cls("LocalJumpError", "unexpected return");
  sp_gc_wb(f);
  f->yielded_value = v;
}

/* ---- the scheduler ---- */

static int fx_sched_started;

static void fx_sched_start(void) {
  if (fx_sched_started) return;
  fx_sched_started = 1;
  /* A kernel that uses threads starts the scheduler in its init. A second
     sp_sched_init would chain the GC hook of the scheduler to itself
     through fx_mark_globals, and a collection would recurse forever. */
  if (sp_Thread_current()) return;
  sp_sched_init();
}

/* At the end of the program: the threads that still run finish first. */
static void fx_sched_end(void) {
  if (fx_sched_started) sp_sched_drain();
}

/* The exception object that Fiber#raise and Thread#raise inject: no
   argument, a message, a class with an optional message, or an object. */
static sp_Exception *fx_fiber_exc(int argc, sp_RbVal *argv) {
  sp_RbVal e;
  if (argc == 0) {
    sp_RbVal m = fx_new_string((const uint8_t *)"unhandled exception", 19);
    SP_GC_ROOT_RBVAL(m);
    e = fx_send(fx_class_box(fx_builtin("RuntimeError")), "new", 1, &m);
  } else if (fx_is_string(argv[0])) {
    e = fx_send(fx_class_box(fx_builtin("RuntimeError")), "new", 1, argv);
  } else if (argv[0].tag == SP_TAG_CLASS) {
    e = fx_send(argv[0], "new", argc > 1 ? 1 : 0, argv + 1);
  } else {
    e = argv[0];
  }
  if (!fx_is_exc(e)) sp_raise_cls("TypeError", "exception class/object expected");
  return (sp_Exception *)e.v.p;
}

/* ---- the class methods ---- */

static int fx_fiber_class(FxClass *k, const char *name, int argc, sp_RbVal *argv, const FxBlock *blk,
                            sp_RbVal *out) {
  if (k->cls_id >= 0) return 0;
  const char *cn = k->name;
  if (!strcmp(cn, "Fiber")) {
    if (FX_IS("new")) {
      if (!blk) sp_raise_cls("ArgumentError", "tried to create Proc object without a block");
      FxFiberData *d = fx_fiber_data(blk, FX_FIB_FIBER);
      SP_GC_ROOT(d);
      sp_Fiber *f = sp_Fiber_new(fx_fiber_body);
      sp_gc_wb(f);
      f->user_data = d;
      *out = sp_box_obj(f, SP_BUILTIN_FIBER);
      return 1;
    }
    if (FX_IS("yield")) { *out = sp_Fiber_yield(fx_fiber_pack(argc, argv)); return 1; }
    if (FX_IS("current") && argc == 0) { *out = sp_box_obj(sp_fiber_current, SP_BUILTIN_FIBER); return 1; }
    if (FX_IS("[]") && argc == 1 && argv[0].tag == SP_TAG_SYM) {
      *out = sp_Fiber_storage_get(sp_fiber_current, (sp_sym)argv[0].v.i);
      return 1;
    }
    if (FX_IS("[]=") && argc == 2 && argv[0].tag == SP_TAG_SYM) {
      sp_Fiber_storage_set(sp_fiber_current, (sp_sym)argv[0].v.i, argv[1]);
      *out = argv[1];
      return 1;
    }
    return 0;
  }
  /* the main thread exists once the scheduler runs */
  if (!strncmp(cn, "Thread", 6)) fx_sched_start();
  if (!strcmp(cn, "Thread")) {
    if (FX_IS("new") || FX_IS("start") || FX_IS("fork")) {
      if (!blk) sp_raise_cls("ThreadError", "must be called with a block");
      FxFiberData *d = fx_fiber_data(blk, FX_FIB_THREAD);
      SP_GC_ROOT(d);
      sp_PolyArray *a = sp_PolyArray_new();
      sp_gc_wb(d);
      d->args = a;
      for (int i = 0; i < argc; i++) sp_PolyArray_push(a, argv[i]);
      sp_Fiber *f = sp_Fiber_new(fx_fiber_body);
      SP_GC_ROOT(f);
      sp_gc_wb(f);
      f->user_data = d;
#ifndef FX_KERNEL
      /* The runtime reads SPINEL_WORKERS at the first spawn. The walker
         runs all the Ruby code of a standalone program, one thread at a
         time under the walker lock, so more OS threads would not run in
         parallel: Fluxpinel asks for one, whatever the program wrote to
         ENV, and main then runs every thread. The host build keeps the
         workers of the kernel. */
      setenv("SPINEL_WORKERS", "1", 1);
#endif
      *out = sp_box_obj(sp_Thread_spawn_fiber(f, sp_box_nil()), SP_BUILTIN_THREAD);
      return 1;
    }
    if (FX_IS("current") && argc == 0) { *out = sp_box_obj(sp_Thread_current(), SP_BUILTIN_THREAD); return 1; }
    if (FX_IS("main") && argc == 0) { *out = sp_box_obj(sp_Thread_main(), SP_BUILTIN_THREAD); return 1; }
    if (FX_IS("pass") && argc == 0) { FX_UNLOCKED(sp_Thread_pass()); *out = sp_box_nil(); return 1; }
    if (FX_IS("list") && argc == 0) {
      sp_PolyArray *a = sp_PolyArray_new();
      SP_GC_ROOT(a);
      for (sp_int i = 0; i < sp_Thread_list_count(); i++)
        sp_PolyArray_push(a, sp_box_obj(sp_Thread_list_at(i), SP_BUILTIN_THREAD));
      *out = sp_box_poly_array(a);
      return 1;
    }
    if (FX_IS("report_on_exception=") && argc == 1) {
      *out = sp_box_bool(sp_Thread_set_report_default(sp_poly_truthy(argv[0])));
      return 1;
    }
    if (FX_IS("report_on_exception") && argc == 0) {
      *out = sp_box_bool(sp_Thread_get_report_default());
      return 1;
    }
    if (FX_IS("abort_on_exception=") && argc == 1) { *out = argv[0]; return 1; }
    return 0;
  }
  if ((!strcmp(cn, "Thread::Queue") || !strcmp(cn, "Thread::SizedQueue")) && FX_IS("new")) {
    if (cn[8] == 'S') {
      if (argc != 1 || argv[0].tag != SP_TAG_INT) return 0;
      *out = sp_box_obj(sp_SizedQueue_new(argv[0].v.i), SP_BUILTIN_QUEUE);
      return 1;
    }
    sp_queue *q = sp_Queue_new();
    SP_GC_ROOT(q);
    if (argc == 1)
      for (sp_int i = 0; i < sp_poly_length(argv[0]); i++) sp_Queue_push(q, sp_poly_arr_get(argv[0], i));
    *out = sp_box_obj(q, SP_BUILTIN_QUEUE);
    return 1;
  }
  if (!strcmp(cn, "Thread::Mutex") && FX_IS("new") && argc == 0) {
    *out = sp_box_obj(sp_Mutex_new(), SP_BUILTIN_MUTEX);
    return 1;
  }
  if (!strcmp(cn, "Thread::ConditionVariable") && FX_IS("new") && argc == 0) {
    *out = sp_box_obj(sp_CondVar_new(), SP_BUILTIN_CONDVAR);
    return 1;
  }
  if (!strcmp(cn, "Enumerator") && FX_IS("new") && blk && argc <= 1) {
    FxFiberData *d = fx_fiber_data(blk, FX_FIB_GEN);
    SP_GC_ROOT(d);
    *out = sp_box_obj(sp_Enumerator_new_gen(fx_fiber_body, d, argc ? argv[0] : sp_box_nil()), SP_BUILTIN_ENUMERATOR);
    return 1;
  }
  return 0;
}

/* ---- the instance methods ---- */

static int fx_fiber_is(sp_RbVal v, sp_int cls) {
  return v.tag == SP_TAG_OBJ && v.cls_id == cls && v.v.p;
}

/* Mutex#synchronize: the lock is released when the block ends, also by a
   raise. */
static sp_RbVal fx_synchronize(sp_mutex *m, const FxBlock *blk) {
  SP_GC_SAVE();
  SP_GC_ROOT(m);
  sp_RbVal v = sp_box_nil();
  SP_GC_ROOT_RBVAL(v);
  const int depth = fx_fs()->depth;
  FX_UNLOCKED(sp_Mutex_lock(m));
  FX_TRY_PUSH();
  if (setjmp(sp_exc_stack[sp_exc_top - 1]) == 0) {
    v = fx_yield(blk, 0, NULL);
    sp_exc_top--;
  } else {
    FX_TRY_LANDED(depth);
    sp_Mutex_unlock(m);
    /* a throw or a proc return goes on to its target */
    if (sp_unwind_kind != SP_UNWIND_NONE) sp_unwind_resume();
    fx_reraise(fx_caught());
  }
  sp_Mutex_unlock(m);
  return v;
}

static int fx_fiber_core(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, const FxBlock *blk,
                           sp_RbVal *out) {
  if (r.tag == SP_TAG_CLASS) {
    FxClass *k = fx_class_value(r);
    return k && fx_fiber_class(k, name, argc, argv, blk, out);
  }
  if (r.tag != SP_TAG_OBJ || !r.v.p) return 0;
  switch (r.cls_id) {
  case SP_BUILTIN_FIBER: {
    sp_Fiber *f = (sp_Fiber *)r.v.p;
    if (FX_IS("resume")) { *out = sp_Fiber_resume_n(f, fx_fiber_pack(argc, argv), argc); return 1; }
    if (FX_IS("transfer")) { *out = sp_Fiber_transfer_n(f, fx_fiber_pack(argc, argv), argc); return 1; }
    if (FX_IS("raise")) {
      sp_Exception *e = fx_fiber_exc(argc, argv);
      SP_GC_ROOT(e);
      *out = sp_Fiber_raise(f, e->cls_name, sp_exc_message(e), e);
      return 1;
    }
    return 0;
  }
  case SP_BUILTIN_THREAD: {
    sp_thread *t = (sp_thread *)r.v.p;
    if (FX_IS("raise")) {
      sp_Exception *e = fx_fiber_exc(argc, argv);
      SP_GC_ROOT(e);
      sp_Thread_raise(t, e->cls_name, sp_exc_message(e), e);
      *out = sp_box_nil();
      return 1;
    }
    if (FX_IS("report_on_exception=") && argc == 1) {
      *out = sp_box_bool(sp_Thread_set_report(t, sp_poly_truthy(argv[0])));
      return 1;
    }
    if (FX_IS("abort_on_exception=") && argc == 1) { *out = argv[0]; return 1; }
    /* the fiber-local slots, by a Symbol or a String */
    if ((FX_IS("[]") || FX_IS("key?")) && argc == 1 && fx_is_string(argv[0])) {
      sp_sym k = sp_sym_intern(sp_poly_recv_s(argv[0], name));
      *out = name[0] == 'k' ? sp_box_bool(sp_Thread_tls_key(t, k)) : sp_Thread_tls_get(t, k);
      return 1;
    }
    if (FX_IS("[]=") && argc == 2 && fx_is_string(argv[0])) {
      *out = sp_Thread_tls_set(t, sp_sym_intern(sp_poly_recv_s(argv[0], name)), argv[1]);
      return 1;
    }
    return 0;
  }
  case SP_BUILTIN_QUEUE: {
    sp_queue *q = (sp_queue *)r.v.p;
    if ((FX_IS("push") || FX_IS("<<") || FX_IS("enq")) && argc == 1) {
      /* a full SizedQueue parks the thread */
      FX_UNLOCKED(sp_Queue_push(q, argv[0]));
      *out = r;
      return 1;
    }
    if (FX_IS("pop") || FX_IS("shift") || FX_IS("deq")) {
      if (argc == 0) { FX_UNLOCKED(*out = sp_Queue_pop(q)); return 1; }
      if (argc == 1 && fx_is_kwhash(argv[0])) {
        sp_RbVal key = sp_box_sym(sp_sym_intern("timeout"));
        sp_RbVal t = fx_send(argv[0], "[]", 1, &key);
        if (t.tag == SP_TAG_NIL) FX_UNLOCKED(*out = sp_Queue_pop(q));
        else FX_UNLOCKED(*out = sp_Queue_pop_timeout(q, sp_poly_to_f(t)));
        return 1;
      }
      if (argc == 1) {
        if (sp_poly_truthy(argv[0])) *out = sp_Queue_pop_nb(q);
        else FX_UNLOCKED(*out = sp_Queue_pop(q));
        return 1;
      }
    }
    return 0;
  }
  case SP_BUILTIN_MUTEX: {
    sp_mutex *m = (sp_mutex *)r.v.p;
    if (FX_IS("synchronize") && argc == 0 && blk) { *out = fx_synchronize(m, blk); return 1; }
    return 0;
  }
  case SP_BUILTIN_CONDVAR: {
    sp_condvar *cv = (sp_condvar *)r.v.p;
    if (FX_IS("wait") && (argc == 1 || argc == 2) && fx_fiber_is(argv[0], SP_BUILTIN_MUTEX)) {
      sp_mutex *m = (sp_mutex *)argv[0].v.p;
      if (argc == 2 && argv[1].tag != SP_TAG_NIL) {
        double t = sp_poly_to_f(argv[1]);
        FX_UNLOCKED(*out = sp_CondVar_wait_timeout(cv, m, t));
      } else {
        FX_UNLOCKED(*out = sp_CondVar_wait(cv, m));
      }
      return 1;
    }
    if (FX_IS("signal") && argc == 0) { sp_CondVar_signal(cv); *out = r; return 1; }
    if (FX_IS("broadcast") && argc == 0) { sp_CondVar_broadcast(cv); *out = r; return 1; }
    return 0;
  }
  case SP_BUILTIN_ENUMERATOR:
    if ((FX_IS("to_a") || FX_IS("entries") || FX_IS("force")) && argc == 0 && !blk) {
      *out = sp_box_poly_array(fx_seq_array(r));
      return 1;
    }
    return 0;
  case SP_BUILTIN_YIELDER:
    /* y << v answers y; y.yield(v) answers what the next #feed gives */
    if (FX_IS("<<") && argc == 1) { sp_Fiber_yield(argv[0]); *out = r; return 1; }
    if (FX_IS("yield") || FX_IS("call")) {
      sp_RbVal v = argc == 1 ? argv[0] : sp_box_nil();
      if (argc > 1) {
        sp_PolyArray *a = sp_PolyArray_new_pack();
        SP_GC_ROOT(a);
        for (int i = 0; i < argc; i++) sp_PolyArray_push(a, argv[i]);
        v = sp_box_poly_array(a);
      }
      *out = sp_Fiber_yield(v);
      return 1;
    }
    return 0;
  default:
    return 0;
  }
}

/* Kernel#sleep: a green thread parks, and the others run. */
static int fx_fiber_kernel(const char *name, int argc, sp_RbVal *argv, sp_RbVal *out) {
  if (!FX_IS("sleep") || argc > 1) return 0;
  /* the threaded runtime parks the sleeper, and the others run */
  if (argc == 0) {
    FX_UNLOCKED(sp_sleep_forever());
    *out = sp_box_int(0);
    return 1;
  }
  sp_float s = sp_poly_to_f(argv[0]);
  if (s < 0) sp_raise_cls("ArgumentError", "time interval must not be negative");
  FX_UNLOCKED(sp_sleep(s));
  *out = sp_box_int((sp_int)(s + 0.5));
  return 1;
}

#undef FX_IS
