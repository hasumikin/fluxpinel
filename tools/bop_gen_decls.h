/* bop_gen_decls.h -- declarations for the spinel sources of bop_gen.

   src/builtin_names.c calls builtin_class_id, but no header that it
   includes declares it. The Makefile gives this declaration to each file
   of bop_gen with -include. tools/bop_gen.c defines the function. */

int builtin_class_id(const char *name);
