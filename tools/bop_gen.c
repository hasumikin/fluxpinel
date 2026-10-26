/* bop_gen.c -- generate Fluxpinel builtin wrappers from spinel's bop_rows[].

   This file includes src/builtin_ops.c, so it reads the static table and
   ty_traits[] of the pinned spinel checkout directly. Build:

     cc -std=gnu11 -I$SPINEL/src -o bop_gen bop_gen.c \
        $SPINEL/src/types.c $SPINEL/src/builtin_names.c

   Usage: bop_gen OUT.c MANIFEST.tsv

   Each BOPE_TEMPLATE row becomes one wrapper per receiver variant:

     sp_RbVal fx_bop_N(sp_RbVal recv, sp_RbVal *argv);

   The wrapper unboxes the receiver and the arguments, evaluates the
   template and boxes the result, with the spellings of ty_traits[]. A
   String argument goes through fx_bop_str, which checks its class. A row
   that needs a fact only the compiler has (a family temp, an argument of
   no single kind, ...) is skipped, and the manifest names the reason.

   Manifest columns: id, row, recv, name, variant, status, reason. The id is
   the wrapper number for status "gen", and -1 for the other statuses:
   "skip" (a template row the generator cannot expand), "emitter" (a row
   with a special codegen emitter) and "none" (a row that only types the
   call; codegen emits it in hand-written code). */

#include "builtin_ops.c"

/* types.c reads this flag of the analysis (src/analyze.c). */
int g_infer_optimistic = 0;

/* builtin_names.c calls this lookup of the analysis (src/analyze_util.c)
   only in builtin_default_cmp, which the generator does not call. */
int builtin_class_id(const char *name) { (void)name; abort(); }

typedef struct {
  TyKind recv;     /* the concrete receiver kind */
  const char *A;   /* $A: sp_<A>Array_* prefix, or NULL */
  const char *H;   /* $H: sp_<H>Hash_* prefix, or NULL */
  TyKind elem;     /* the element kind of an Array, or TY_UNKNOWN */
} Variant;

typedef struct { char *p; size_t len, cap; } Str;

static void str_putn(Str *s, const char *t, size_t n) {
  if (s->len + n + 1 > s->cap) {
    s->cap = (s->len + n + 1) * 2;
    s->p = realloc(s->p, s->cap);
  }
  memcpy(s->p + s->len, t, n);
  s->len += n;
  s->p[s->len] = 0;
}
static void str_puts(Str *s, const char *t) { str_putn(s, t, strlen(t)); }
static void str_printf(Str *s, const char *fmt, ...) {
  char buf[512];
  va_list ap;
  va_start(ap, fmt);
  int n = vsnprintf(buf, sizeof buf, fmt, ap);
  va_end(ap);
  str_putn(s, buf, (size_t)n);
}

static int g_temp;

/* ty_traits_render: $e is the expression, $t the first temp of the form */
static void render(Str *s, const char *cell, const char *expr) {
  int tb = 0;
  for (const char *p = cell; *p; p++) {
    if (p[0] == '$' && p[1] == 'e') { str_puts(s, expr); p++; continue; }
    if (p[0] == '$' && p[1] == 't') { if (!tb) tb = ++g_temp; str_printf(s, "%d", tb); p++; continue; }
    str_putn(s, p, 1);
  }
}

/* The one kind of a BopKinds set, or TY_UNKNOWN for none or several. */
static TyKind single_kind(BopKinds ks) {
  if (!ks || (ks & (ks - 1))) return TY_UNKNOWN;
  TyKind k = 0;
  while (!(ks & 1)) { ks >>= 1; k++; }
  return k;
}

/* Renders the unbox of `expr` into kind k. Answers 0 when k has no row. */
static int unbox(Str *s, TyKind k, const char *expr) {
  if (k == TY_POLY) { str_puts(s, expr); return 1; }
  const TyTraits *tr = ty_traits_of(k);
  if (!tr || !tr->unbox) return 0;
  render(s, tr->unbox, expr);
  return 1;
}

/* Expands the template of op for variant v into s. Answers NULL, or the
   reason the row needs a compiler fact. *oint is set when the template
   leaves its result as an sp_oint / sp_ofloat ($< ... $>). */
static const char *expand(Str *s, const BuiltinOp *op, const Variant *v, int *oint) {
  static const char tnames[] = "tuvwxyz";
  int tn[7] = { 0 };
  *oint = 0;
  for (int k = 0; k < 7; k++) {
    char pat[3] = { '$', tnames[k], 0 };
    if (strstr(op->arg, pat)) tn[k] = ++g_temp;
  }
  for (const char *p = op->arg; *p; p++) {
    if (p[0] != '$' || !p[1]) { str_putn(s, p, 1); continue; }
    char c = p[1];
    int n = p[2] >= '0' && p[2] <= '9' ? p[2] - '0' : -1;
    const char *tk = strchr(tnames, c);
    char arg[32];
    if (n >= 0) snprintf(arg, sizeof arg, "argv[%d]", n);
    switch (c) {
    case 'r': str_puts(s, "r"); p++; continue;
    case 'h': str_puts(s, "r"); p++; continue;   /* the held receiver: r is a local already */
    case 'g': str_printf(s, "r; SP_GC_ROOT(_t%d); ", tn[0]); p++; continue;
    case '<': case '>': *oint = 1; p++; continue;
    case 'O':
      str_puts(s, v->recv == TY_INT_ARRAY || v->recv == TY_FLOAT_ARRAY ? "_o" : "");
      p++; continue;
    case 'N':
      if (v->recv == TY_INT) str_puts(s, "sp_oint_of(r)");
      else if (v->recv == TY_FLOAT) str_puts(s, "sp_ofloat_of(r)");
      else return "$N on a non-numeric receiver";
      p++; continue;
    case 'A': if (!v->A) return "$A without an Array variant"; str_puts(s, v->A); p++; continue;
    case 'H': if (!v->H) return "$H without a Hash variant"; str_puts(s, v->H); p++; continue;
    case 'K': {
      const TyTraits *tr = ty_traits_of(v->recv);
      if (!tr || !tr->hash_id) return "$K without a hash id";
      str_puts(s, tr->hash_id); p++; continue;
    }
    /* $C: the suffix of a literal case map option. A wrapper maps with no option. */
    case 'C': p++; continue;
    case 'T': return "$T (family temp)";
    case 'B': case 'E': return "$B/$E (String Range handle)";
    case 'R': return "$R";
    case '[': return "$[";
    default: break;
    }
    if (tk && n < 0) { str_printf(s, "%d", tn[tk - tnames]); p++; continue; }
    if (n < 0) return "unknown placeholder";
    switch (c) {
    case 'b': str_puts(s, arg); break;
    case 'i': case 'o': unbox(s, TY_INT, arg); break;
    case 'f': unbox(s, TY_FLOAT, arg); break;
    case 's': str_printf(s, "fx_bop_str(%s)", arg); break;
    case 'c': str_printf(s, "sp_poly_as_complex(%s)", arg); break;
    case 'q': str_printf(s, "sp_poly_as_rational(%s)", arg); break;
    case 'F': return "$F (Float coercion)";
    case 'e': {
      TyKind k = n == 0 ? single_kind(op->arg0) : n == 1 ? single_kind(op->arg1) : TY_UNKNOWN;
      if (k == TY_UNKNOWN) return "$e of no single kind";
      if (!unbox(s, k, arg)) return "$e of a kind with no unbox";
      break;
    }
    default: return "unknown placeholder";
    }
    p += 2;
  }
  return NULL;
}

/* The kind the call answers for variant v, or TY_UNKNOWN. */
static TyKind result_kind(const BuiltinOp *op, const Variant *v) {
  if (op->result == BOPR_SELF) return v->recv;
  if (op->result == BOPR_ELEM) return v->elem;
  if ((int)op->result < 0) return TY_UNKNOWN;
  return op->result;
}

/* Boxes expr of kind k into s. Answers NULL, or the reason it cannot. */
static const char *box(Str *s, TyKind k, int oint, const char *expr) {
  if (k == TY_UNKNOWN) return "result of no kind";
  if (k == TY_POLY) { str_puts(s, expr); return NULL; }
  if (k == TY_NIL) { str_printf(s, "({ (void)(%s); sp_box_nil(); })", expr); return NULL; }
  /* Some runtime helpers answer an sp_oint / sp_ofloat where the row says
     Integer / Float (IO#getbyte). Only the C type tells, so box by it. */
  if (k == TY_INT) { str_printf(s, "FX_BOX_INT(%s)", expr); return NULL; }
  if (k == TY_FLOAT) { str_printf(s, "FX_BOX_FLOAT(%s)", expr); return NULL; }
  const TyTraits *tr = ty_traits_of(k);
  if (!tr) return "result kind with no box";
  /* box_nil names a function (sp_box_oint); box_text renders $e */
  if (oint && tr->box_nil) { str_printf(s, "%s(%s)", tr->box_nil, expr); return NULL; }
  if (!tr->box_text) return "result kind with no box";
  render(s, tr->box_text, expr);
  return NULL;
}

/* What the lookup table says about each wrapper. */
typedef struct {
  const char *recv, *name, *variant;
  int argc_min, argc_max;
  unsigned long long arg0, arg1;
} Wrapper;
#define MAX_WRAPPERS 4096
static Wrapper wrappers[MAX_WRAPPERS];

static int variants_of(TyKind recv, Variant *out) {
  if (recv == BOP_ANY_ARRAY) {
    out[0] = (Variant){ TY_INT_ARRAY, "Int", NULL, TY_INT };
    out[1] = (Variant){ TY_FLOAT_ARRAY, "Float", NULL, TY_FLOAT };
    out[2] = (Variant){ TY_STR_ARRAY, "Str", NULL, TY_STRING };
    out[3] = (Variant){ TY_POLY_ARRAY, "Poly", NULL, TY_POLY };
    return 4;
  }
  if (recv == BOP_ANY_HASH) {
    int n = 0;
    for (int k = 0; k < TY_TRAITS_N; k++)
      if (ty_is_hash((TyKind)k)) out[n++] = (Variant){ (TyKind)k, NULL, ty_hash_cname((TyKind)k), TY_UNKNOWN };
    return n;
  }
  if ((int)recv < 0) return 0;
  out[0] = (Variant){ recv, NULL, NULL, TY_UNKNOWN };
  return 1;
}

int main(int argc, char **argv) {
  if (argc != 3) { fprintf(stderr, "usage: bop_gen OUT.c MANIFEST.tsv\n"); return 2; }
  FILE *out = fopen(argv[1], "w"), *man = fopen(argv[2], "w");
  if (!out || !man) { perror("bop_gen"); return 1; }
  fputs("/* Generated by fluxpinel tools/bop_gen.c from spinel bop_rows[]. Do not edit.\n"
        "   Include it after fx_tu.c, in the same TU. */\n"
        "#include \"fx_tu.h\"\n"
        "#define FX_BOX_INT(x) _Generic((x), sp_oint: sp_box_oint, default: sp_box_int)(x)\n"
        "#define FX_BOX_FLOAT(x) _Generic((x), sp_ofloat: sp_box_ofloat, default: sp_box_float)(x)\n"
        "/* A String argument ($s): nil stays NULL as sp_poly_unbox_s gives it,\n"
        "   and any other value that is not a String is the TypeError of CRuby. */\n"
        "static const char *fx_bop_str(sp_RbVal v) {\n"
        "  return v.tag == SP_TAG_NIL ? sp_poly_unbox_s(v) : sp_poly_arg_str_chk(v);\n"
        "}\n",
        out);
  /* the TyKind numbers of the pinned spinel, for the argument kind masks */
  static const struct { const char *name; TyKind kind; } kinds[] = {
    { "NIL", TY_NIL }, { "INT", TY_INT }, { "FLOAT", TY_FLOAT }, { "STRING", TY_STRING },
    { "SYMBOL", TY_SYMBOL }, { "BOOL", TY_BOOL }, { "POLY", TY_POLY },
    { "INT_ARRAY", TY_INT_ARRAY }, { "FLOAT_ARRAY", TY_FLOAT_ARRAY },
    { "STR_ARRAY", TY_STR_ARRAY }, { "POLY_ARRAY", TY_POLY_ARRAY }, { "RANGE", TY_RANGE },
    { "FLOAT_RANGE", TY_FLOAT_RANGE }, { "STR_RANGE", TY_STR_RANGE },
    { "STR_INT_HASH", TY_STR_INT_HASH }, { "STR_STR_HASH", TY_STR_STR_HASH },
    { "INT_INT_HASH", TY_INT_INT_HASH }, { "INT_STR_HASH", TY_INT_STR_HASH },
    { "SYM_POLY_HASH", TY_SYM_POLY_HASH }, { "STR_POLY_HASH", TY_STR_POLY_HASH },
    { "POLY_POLY_HASH", TY_POLY_POLY_HASH }, { "REGEX", TY_REGEX }, { "MATCHDATA", TY_MATCHDATA },
    { "IO", TY_IO }, { "FIBER", TY_FIBER }, { "THREAD", TY_THREAD }, { "ENUMERATOR", TY_ENUMERATOR },
    { "QUEUE", TY_QUEUE }, { "MUTEX", TY_MUTEX }, { "CONDVAR", TY_CONDVAR },
    { "RATIONAL", TY_RATIONAL }, { "TIME", TY_TIME }, { "COMPLEX", TY_COMPLEX },
  };
  for (size_t k = 0; k < sizeof kinds / sizeof kinds[0]; k++)
    fprintf(out, "#define FX_TY_%s %d\n", kinds[k].name, (int)kinds[k].kind);
  int id = 0;
  size_t nrows = sizeof bop_rows / sizeof bop_rows[0];
  for (size_t i = 0; i < nrows; i++) {
    const BuiltinOp *op = &bop_rows[i];
    if (op->emit != BOPE_TEMPLATE) {
      const char *rn = op->recv == BOP_ANY_ARRAY ? "ANY_ARRAY" : op->recv == BOP_ANY_HASH ? "ANY_HASH" :
                       (int)op->recv < 0 ? "family" : ty_name(op->recv);
      fprintf(man, "-1\t%zu\t%s\t%s\t-\t%s\temit %d\n", i, rn, op->name ? op->name : "",
              op->emit == BOPE_NONE ? "none" : "emitter", (int)op->emit);
      continue;
    }
    Variant vs[32];
    int nv = variants_of(op->recv, vs);
    if (nv == 0) {
      fprintf(man, "-1\t%zu\t%d\t%s\t-\tskip\treceiver family with no variants\n", i, (int)op->recv, op->name);
      continue;
    }
    for (int j = 0; j < nv; j++) {
      const Variant *v = &vs[j];
      const char *vname = ty_name(v->recv);
      const char *rname = (int)op->recv < 0 ? (op->recv == BOP_ANY_ARRAY ? "ANY_ARRAY" : "ANY_HASH") : vname;
      const TyTraits *rt = ty_traits_of(v->recv);
      Str body = { 0 }, boxed = { 0 }, recv = { 0 };
      g_temp = 0;
      int oint = 0;
      const char *why = NULL;
      if (!rt || !rt->ctype) why = "receiver kind with no C type";
      else if (!unbox(&recv, v->recv, "recv")) why = "receiver kind with no unbox";
      if (!why) why = expand(&body, op, v, &oint);
      if (!why) why = box(&boxed, result_kind(op, v), oint, body.p);
      if (why) {
        fprintf(man, "-1\t%zu\t%s\t%s\t%s\tskip\t%s\n", i, rname, op->name, vname, why);
      } else {
        fprintf(out, "\n/* row %zu: %s#%s (%s) */\n", i, rname, op->name, vname);
        fprintf(out, "#ifndef FX_BOP_OFF_%d\n", id);
        fprintf(out, "sp_RbVal fx_bop_%d(sp_RbVal recv, sp_RbVal *argv) {\n", id);
        fprintf(out, "  (void)argv;\n  %s r = %s;\n  return %s;\n}\n#endif\n", rt->ctype, recv.p, boxed.p);
        fprintf(man, "%d\t%zu\t%s\t%s\t%s\tgen\t\n", id, i, rname, op->name, vname);
        if (id >= MAX_WRAPPERS) { fprintf(stderr, "bop_gen: more than %d wrappers\n", MAX_WRAPPERS); return 1; }
        wrappers[id] = (Wrapper){ rname, op->name, vname, op->argc_min, op->argc_max, op->arg0, op->arg1 };
        id++;
      }
      free(body.p); free(boxed.p); free(recv.p);
    }
  }
  /* arg0 / arg1: the kinds the first and second argument may have, one bit
     per TyKind (BOP_K), or 0 for any kind */
  fputs("\ntypedef sp_RbVal (*fx_bop_fn)(sp_RbVal, sp_RbVal *);\n"
        "typedef struct { const char *recv, *name, *variant; int argc_min, argc_max;\n"
        "                 unsigned long long arg0, arg1; fx_bop_fn fn; } FxBop;\n",
        out);
  fprintf(out, "const int fx_bop_count = %d;\nconst FxBop fx_bop_table[] = {\n", id);
  for (int k = 0; k < id; k++) {
    const Wrapper *w = &wrappers[k];
    fprintf(out, "  { \"%s\", \"%s\", \"%s\", %d, %d, %#llxULL, %#llxULL,\n"
                 "#ifndef FX_BOP_OFF_%d\n    fx_bop_%d },\n#else\n    0 },\n#endif\n",
            w->recv, w->name, w->variant, w->argc_min, w->argc_max, w->arg0, w->arg1, k, k);
  }
  fputs("};\n", out);
  fclose(out);
  fclose(man);
  return 0;
}
