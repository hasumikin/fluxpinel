# A backtrace in a file that require_relative or load runs: its top level
# is "<top (required)>", a block there "block in <top (required)>", and
# the call is a frame of Kernel ("Kernel#require_relative", "Kernel#load")
# over the frame of the caller.
require_relative "requires/trace"
load File.join(__dir__, "requires/trace.rb")

def from_method
  load File.join(__dir__, "requires/trace.rb")
end
from_method
puts caller(0).map { |l| l.sub(/\A.*\//, "") }
