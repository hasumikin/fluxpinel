# irb_did_you_mean.rb -- the "Did you mean?" line of an error in IRB.
#
# CRuby loads the did_you_mean gem at its start. Fluxpinel loads it for a
# session of IRB (--irb, --irb-port), when the build found the gem in a
# directory of its own (tools/gem_dirs.rb). The gem gives a NameError, a
# NoMethodError and a KeyError a detailed_message with the names that are
# near the wrong one.

begin
  require "did_you_mean"
rescue LoadError
  # no gem: an error has its plain message
end
