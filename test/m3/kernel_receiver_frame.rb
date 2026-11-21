# The module functions of Kernel that read the frame of their call work
# with Kernel as the receiver too: Kernel.eval sees the caller's locals,
# Kernel.binding is the caller's, Kernel.caller lists the caller's frames,
# and Kernel.sleep and Kernel.require are the functions.
a = 5
p Kernel.eval("a + 1"), Kernel.eval("a = 7"), a
b = Kernel.binding
p b.local_variable_get(:a), b.local_variables.include?(:a)
def frames = Kernel.caller(0, 2).map { |l| l[/'.*'/] }
p frames
def inner_eval
  x = 3
  Kernel.eval("x * 2")
end
p inner_eval
p Kernel.sleep(0), Kernel.require("set"), Kernel.require("set")
p Kernel.respond_to?(:eval), Kernel.respond_to?(:sleep), Kernel.respond_to?(:binding)
begin
  Kernel.eval("1 +")
rescue SyntaxError => e
  p e.class
end
p ::Kernel.eval("a - 1"), Kernel.caller_locations(0, 1).first.lineno

# a method the program gives the module comes first
def Kernel.sleep(n) = "sleep:#{n}"
module Kernel
  def self.caller(*) = :own
end
p Kernel.sleep(0), Kernel.caller(0), Kernel.eval("a")
