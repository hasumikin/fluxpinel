# irb_did_you_mean.rb -- the "Did you mean?" line of an error in IRB.
#
# CRuby loads the did_you_mean gem at its start. Fluxpinel loads it for a
# session of IRB (--irb, --irb-port), when the build found the gem in a
# directory of its own (tools/gem_dirs.rb). The gem gives a NameError, a
# NoMethodError and a KeyError a detailed_message with the names that are
# near the wrong one.
#
# A wrong constant has no such line yet: the gem keeps the names of the
# constants in instances of a subclass of String, which Fluxpinel does not
# have. The detailed_message of the gem rescues a StandardError and gives
# the plain message; the NotImplementedError of Fluxpinel is a ScriptError,
# so the checker of the constants is turned off here.

begin
  require "did_you_mean"
rescue LoadError
  # no gem: an error has its plain message
else
  module Fluxpinel
    # The checker of a NameError of the gem without the constants.
    module NameErrorCheckers
      def self.new(exception)
        if exception.message.start_with?("uninitialized constant")
          DidYouMean::NullChecker.new(exception)
        else
          DidYouMean::NameErrorCheckers.new(exception)
        end
      end
    end
  end
  DidYouMean.correct_error(NameError, Fluxpinel::NameErrorCheckers)
end
