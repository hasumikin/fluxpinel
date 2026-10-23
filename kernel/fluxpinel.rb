# fluxpinel.rb -- the calls of an AOT program into Fluxpinel (docs/design.md,
# section 5).
#
# The program that spinel compiles as the kernel of the host build does
# require_relative of this file. Each function is a C function of Fluxpinel in
# the same TU (src/host.c), and a value crosses as it is (:any).
#
# - Fluxpinel.load(path) runs a Ruby file at the top level and answers the value
#   of its last statement. Each call runs the file again.
# - Fluxpinel.eval(src) does the same for a String.
# - Fluxpinel.call(obj, name, args) calls the method name of a value of Fluxpinel
#   with the Array args. The poly dispatch of AOT code does not know the
#   classes of Fluxpinel, so AOT code calls their methods through this.
# - Fluxpinel.session answers a new console session (src/console.c).
# - Fluxpinel.feed(session, line) adds a line to the session, and evaluates
#   the piece when it is complete. It answers the reply and the next prompt:
#   "=> value\nfx> ", "fx* " for more lines, or the exception. A local of a
#   piece stays for the next pieces, and `_` holds the last value. An exit
#   in a piece closes the session, and feed answers nil from then on.
#
# Call them from an entry: Fluxpinel starts after the top level of the kernel.
# Under CRuby, fluxpinel_cruby.rb gives the same functions.

module Fluxpinel
  native_func :load, [:string], :any, "fx_api_load"
  native_func :eval, [:string], :any, "fx_api_eval"
  native_func :call, [:any, :string, :any], :any, "fx_api_call"
  native_func :session, [], :any, "fx_api_session"
  native_func :feed, [:any, :string], :string, "fx_api_feed"
end
