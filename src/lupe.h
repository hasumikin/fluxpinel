/* lupe.h -- shared declarations of the Lupe walker.

   Lupe is one TU: main.c includes every other .c file (see lupe_tu.c for
   why). This header holds the types that those files share. */

#ifndef LUPE_H
#define LUPE_H

#include "lupe_tu.h"
#include "prism.h"

/* A method that the program defines with `def` at the top level. */
typedef struct {
  sp_sym name;
  const pm_def_node_t *def;
} LupeMethod;

/* The local variables of one scope (the program, or one method call).
   locals[i] holds the variable names->ids[i]; each slot is a GC root. */
typedef struct {
  sp_RbVal *locals;
  const pm_constant_id_list_t *names;
} LupeFrame;

/* A pending jump out of the statement under evaluation. */
typedef enum { LUPE_CTL_NONE, LUPE_CTL_RETURN, LUPE_CTL_BREAK, LUPE_CTL_NEXT } LupeCtl;

typedef struct {
  const char *path;
  pm_parser_t parser;
  LupeMethod *methods;
  int nmethods, methods_cap;
  LupeCtl ctl;
  sp_RbVal ctl_val;   /* the value that the jump carries; a GC root */
} Lupe;

static Lupe L;

static sp_RbVal lupe_eval(LupeFrame *f, const pm_node_t *n);
static sp_RbVal lupe_send(sp_RbVal recv, const char *name, int argc, sp_RbVal *argv);
static int lupe_kernel(const char *name, int argc, sp_RbVal *argv, sp_RbVal *out);
static __attribute__((noreturn)) void lupe_unsupported(const pm_node_t *n, const char *what);

#endif
