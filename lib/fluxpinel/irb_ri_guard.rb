# irb_ri_guard.rb -- the completion dialog of IRB with no ri driver.
#
# IRB::RelineInputMethod#rdoc_ri_driver rescues the LoadError of
# `require 'rdoc'` only. rdoc 8.1 requires rbs when the ri driver starts,
# and rbs needs a C extension, which Fluxpinel cannot load. The LoadError
# goes up past Reline and ends the session when the dialog opens.
#
# The guard rescues the LoadError of the driver too. With no driver the
# dialog comes with no document. --irb and --irb-port load this file.
# An IRB with ruby/irb#1272 does not need it.

require "irb"

module Fluxpinel
  module IrbRiGuard
    def rdoc_ri_driver
      super
    rescue LoadError
      @rdoc_ri_driver = nil
    end
  end
end

IRB::RelineInputMethod.prepend(Fluxpinel::IrbRiGuard)
