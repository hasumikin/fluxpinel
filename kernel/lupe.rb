# lupe.rb -- the calls of an AOT program into Lupe (docs/design.md,
# section 5).
#
# The program that spinel compiles as the kernel of the host build does
# require_relative of this file. Each function is a C function of Lupe in
# the same TU (src/host.c), and a value crosses as it is (:any).
#
# - Lupe.load(path) runs a Ruby file at the top level and answers the value
#   of its last statement. Each call runs the file again.
# - Lupe.eval(src) does the same for a String.
# - Lupe.call(obj, name, args) calls the method name of a value of Lupe
#   with the Array args. The poly dispatch of AOT code does not know the
#   classes of Lupe, so AOT code calls their methods through this.
#
# Call them from an entry: Lupe starts after the top level of the kernel.
# Under CRuby, lupe_cruby.rb gives the same functions.

module Lupe
  native_func :load, [:string], :any, "lupe_api_load"
  native_func :eval, [:string], :any, "lupe_api_eval"
  native_func :call, [:any, :string, :any], :any, "lupe_api_call"
end
