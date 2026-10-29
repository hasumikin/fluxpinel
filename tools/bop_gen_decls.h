/* bop_gen_decls.h -- declarations for the spinel sources of bop_gen.

   src/builtin_names.c calls these lookups of the analysis, but no header
   that it includes declares them. The Makefile gives these declarations
   to each file of bop_gen with -include. tools/bop_gen.c defines the
   functions. */

int builtin_class_id(const char *name);
int is_builtin_class_name(const char *n);
int is_builtin_module_name(const char *n);
int is_builtin_exception_name(const char *n);
