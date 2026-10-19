/* lupe.h -- shared declarations of the Lupe walker.

   Lupe is one TU: main.c includes every other .c file (see lupe_tu.c for
   why). This header holds the types that those files share. */

#ifndef LUPE_H
#define LUPE_H

#include "lupe_tu.h"
#include "prism.h"

struct LupeClass;

/* What a method does when it is called. */
typedef enum { LUPE_M_DEF, LUPE_M_READER, LUPE_M_WRITER } LupeMethodKind;

typedef struct {
  sp_sym name;
  LupeMethodKind kind;
  const pm_def_node_t *def;   /* LUPE_M_DEF */
  sp_sym ivar;                /* LUPE_M_READER / LUPE_M_WRITER: "@name" */
  struct LupeClass *owner;    /* the class or module that defines it */
} LupeMethod;

typedef struct {
  LupeMethod **items;
  int n, cap;
} LupeMethodTable;

typedef struct {
  sp_sym name;
  sp_RbVal value;   /* marked by the globals hook */
} LupeConst;

/* A class or a module. A builtin class (Integer, Object, ...) gets one
   too, on first use, so that a program can reopen it and so that constant
   lookup and is_a? see one hierarchy. */
typedef struct LupeClass {
  sp_int cls_id;                    /* the runtime id: builtin < 0, Lupe >= LUPE_CLS_BASE */
  sp_int inst_id;                   /* the id that an instance carries: cls_id for a Lupe
                                       class; an alias >= LUPE_CLS_BASE for Object */
  const char *name;                 /* "A::B" */
  int is_module;
  struct LupeClass *super;          /* NULL for BasicObject and for a module */
  struct LupeClass *lexical;        /* the enclosing class or module, for constants */
  LupeMethodTable methods;          /* instance methods */
  LupeMethodTable smethods;         /* singleton methods (class methods) */
  struct LupeClass **includes;      /* in include order */
  int nincludes, includes_cap;
  struct LupeClass **extends;       /* modules extended into the singleton */
  int nextends, extends_cap;
  LupeConst *consts;
  int nconsts, consts_cap;
  sp_sym *ivars;                    /* the slot names of an instance */
  int nivars, ivars_cap;
} LupeClass;

/* An instance of a Lupe class. It starts like a spinel object (cls_id
   first), so the runtime reads its class id as it reads an AOT object's. */
typedef struct {
  sp_int cls_id;
  sp_PolyArray *iv;   /* slot i holds the ivar cls->ivars[i] */
} LupeObject;

/* Lupe class ids start here, far above the ids that spinel codegen gives
   the classes of an AOT program (0, 1, ...). */
#define LUPE_CLS_BASE 0x100000

struct LupeFrame;

/* A block that a call passes: a block literal, a lambda, `&:sym` or the
   block of a Proc. A block literal lives in the C frame of its call. */
typedef struct LupeBlock {
  const pm_node_t *node;          /* a block or a lambda node; NULL for &:sym */
  struct LupeFrame *home;         /* the frame where the block was written */
  uint64_t serial;                /* home->serial then; another value means home returned */
  const struct LupeBlock *site;   /* the call that break leaves; NULL for none */
  sp_sym sym;                     /* &:sym */
  int lambda;
  void *proc;                     /* the LupeProc that carries the block, or NULL */
} LupeBlock;

/* A Proc value. It starts with a spinel sp_Proc, so that the runtime reads
   the fields that it knows (lambda?, frozen?). The runtime cannot call it:
   fn is NULL, and Lupe answers every call of a Proc itself. */
typedef struct {
  sp_Proc p;
  LupeBlock b;
} LupeProc;

/* The local variables and the context of one scope: the program, a class
   body, a method call or a block call. locals[i] holds the variable
   names->ids[i]; each slot is a GC root. */
typedef struct LupeFrame {
  sp_RbVal *locals;
  const pm_constant_id_list_t *names;
  sp_RbVal self;              /* rooted by the caller */
  LupeClass *cref;            /* the lexical class: def target and constant scope */
  const LupeMethod *method;   /* the running method, for super; NULL outside one */
  int singleton_defs;         /* 1: inside class << self; 2: after module_function */
  struct LupeFrame *parent;   /* a block frame: the frame of the enclosing scope */
  const LupeBlock *block;     /* the block of the method, for yield; NULL for none */
  sp_RbVal *it;               /* the slot of `it` (a rooted slot after the locals) */
  sp_RbVal *anon;             /* [0]: anonymous *rest, [1]: anonymous **kwrest, also
                                 what `...` takes (rooted slots after `it`) */
  uint64_t serial;            /* unique to this frame, for LupeBlock.serial */
  int lambda;                 /* a lambda frame: return leaves it */
} LupeFrame;

/* A pending jump out of the statement under evaluation. A break in a
   block becomes LUPE_CTL_BREAK_BLOCK at the block boundary; it ends the
   call that passed the block (L.ctl_target). A return ends the frame
   L.ctl_target. */
typedef enum {
  LUPE_CTL_NONE, LUPE_CTL_RETURN, LUPE_CTL_BREAK, LUPE_CTL_NEXT, LUPE_CTL_BREAK_BLOCK
} LupeCtl;

typedef struct {
  const char *path;
  pm_parser_t parser;
  LupeClass **classes;   /* Lupe classes; classes[i] has cls_id LUPE_CLS_BASE + i */
  int nclasses, classes_cap;
  LupeClass **builtins;  /* builtin classes made so far */
  int nbuiltins, builtins_cap;
  LupeClass *object;     /* Object: top-level def and constants go here */
  sp_RbVal main;         /* the top-level self; a GC root */
  LupeCtl ctl;
  sp_RbVal ctl_val;      /* the value that the jump carries; a GC root */
  const void *ctl_target;   /* LUPE_CTL_RETURN: a LupeFrame; LUPE_CTL_BREAK_BLOCK: a LupeBlock */
  uint64_t serial;       /* the last LupeFrame.serial */
  const void *kwhash;    /* the Hash that a call passes as keywords (`m(a: 1)`), so
                            that a Hash literal in the last place stays positional */
} Lupe;

static Lupe L;

static sp_RbVal lupe_eval(LupeFrame *f, const pm_node_t *n);
static sp_RbVal lupe_send(sp_RbVal recv, const char *name, int argc, sp_RbVal *argv);
static sp_RbVal lupe_send_b(sp_RbVal recv, const char *name, int argc, sp_RbVal *argv, const LupeBlock *blk);
static sp_RbVal lupe_call_self(LupeFrame *f, const char *name, int argc, sp_RbVal *argv, const LupeBlock *blk);
static sp_RbVal lupe_yield(const LupeBlock *b, int argc, sp_RbVal *argv);
static int lupe_iter(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, const LupeBlock *blk, sp_RbVal *out);
static int lupe_proc_core(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out);
static int lupe_range_core(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out);
static int lupe_hash_core(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, const LupeBlock *blk,
                          sp_RbVal *out);
static int lupe_array_hash(sp_RbVal r, const char *name, int argc, sp_RbVal *out);
static int lupe_is_proc(sp_RbVal v);
static int lupe_is_range(sp_RbVal v);
static int lupe_is_hash(sp_RbVal v);
static int lupe_kernel(const char *name, int argc, sp_RbVal *argv, sp_RbVal *out);
static void lupe_trace_missing(const char *cls, const char *name, int argc);
static int lupe_builtin_responds(sp_RbVal r, const char *name);
static __attribute__((noreturn)) void lupe_unsupported(const pm_node_t *n, const char *what);
static __attribute__((noreturn)) void lupe_gap(const char *what);
static sp_sym lupe_sym(pm_constant_id_t id);

/* object.c */
static LupeClass *lupe_class_of(sp_RbVal v);
static LupeClass *lupe_class_value(sp_RbVal v);
static sp_RbVal lupe_class_box(const LupeClass *c);
static LupeMethod *lupe_find_method(const LupeClass *c, sp_sym name);
static LupeMethod *lupe_find_smethod(const LupeClass *c, sp_sym name);
static LupeMethod *lupe_find_super(const LupeClass *c, const LupeMethod *m);
static sp_RbVal lupe_invoke(const LupeMethod *m, sp_RbVal self, int argc, sp_RbVal *argv);
static sp_RbVal lupe_invoke_b(const LupeMethod *m, sp_RbVal self, int argc, sp_RbVal *argv, const LupeBlock *blk);
static int lupe_object_core(sp_RbVal r, const char *name, int argc, sp_RbVal *argv, sp_RbVal *out);
static int lupe_class_core(LupeFrame *f, sp_RbVal r, const char *name, int argc, sp_RbVal *argv,
                           const LupeBlock *blk, sp_RbVal *out);
static int lupe_is_lupe_object(sp_RbVal v);
static int lupe_unknown_builtin(const char *name);

#endif
