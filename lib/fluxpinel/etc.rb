# etc.rb -- Etc.nprocessors, in Ruby over libc.
#
# spinel's etc package does not have it. Fluxpinel adds it when the package
# loads (src/load.c). The answer is the count of the processors that are
# online (sysconf). CRuby on Linux answers the count that the process can
# run on, which is smaller under a limit of the CPU set (taskset, a
# container).
module Etc
  module Native
    native_func :sysconf, [:int], :int, "sysconf"
  end

  # _SC_NPROCESSORS_ONLN: 58 on macOS, 84 on Linux
  def self.nprocessors
    n = Native.sysconf(RUBY_PLATFORM.include?("darwin") ? 58 : 84)
    n < 1 ? 1 : n
  end
end
