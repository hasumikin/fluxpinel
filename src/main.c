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

static char *lupe_read_file(const char *path, size_t *len) {
  FILE *fp = fopen(path, "rb");
  if (!fp) { fprintf(stderr, "lupe: cannot open %s\n", path); exit(1); }
  fseek(fp, 0, SEEK_END);
  long n = ftell(fp);
  fseek(fp, 0, SEEK_SET);
  char *buf = malloc((size_t)n + 1);
  if (!buf || fread(buf, 1, (size_t)n, fp) != (size_t)n) { fprintf(stderr, "lupe: cannot read %s\n", path); exit(1); }
  fclose(fp);
  buf[n] = 0;
  *len = (size_t)n;
  return buf;
}

static void lupe_body(void) {
  SP_GC_SAVE();
  lupe_tu_init();
  L.ctl_val = sp_box_nil();
  _sp_gc_root_push((void **)((uintptr_t)&L.ctl_val | (uintptr_t)1));
  lupe_object_init();

  size_t len;
  char *src = lupe_read_file(L.path, &len);
  pm_parser_init(&L.parser, (const uint8_t *)src, len, NULL);
  pm_node_t *root = pm_parse(&L.parser);
  if (L.parser.error_list.size) {
    const pm_diagnostic_t *d = (const pm_diagnostic_t *)L.parser.error_list.head;
    int line = pm_newline_list_line_column(&L.parser.newline_list, d->location.start, L.parser.start_line).line;
    fprintf(stderr, "lupe: %s:%d: syntax error: %s\n", L.path, line, d->message);
    exit(1);
  }
  const pm_program_node_t *prog = (const pm_program_node_t *)root;
  LUPE_FRAME(top, &prog->locals, L.main, L.object);
  lupe_eval_stmts(&top, prog->statements);
  sp_fin_run_exit();
}

int main(int argc, char **argv) {
  if (argc != 2) { fprintf(stderr, "usage: lupe FILE.rb\n"); return 2; }
  L.path = argv[1];
  sp_main_stack_run(lupe_body);
  return 0;
}
