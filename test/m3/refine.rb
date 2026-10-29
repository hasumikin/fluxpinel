# Refinements: refine in a module, using at the top level and in a class
# body, super from a refined method, and the calls that see none.

class Greeter
  def hi = "hi"
  def name = "greeter"
end

module Loud
  refine Greeter do
    def hi = super.upcase + "!"
    def shout = "#{name.upcase}!!"
  end
  refine String do
    def shout = upcase + "!"
  end
end

module Quiet
  refine Greeter do
    def hi = "..."
  end
end

g = Greeter.new

# before using: nothing changes
p g.hi
p g.respond_to?(:shout)
begin
  g.shout
rescue NoMethodError => e
  puts e.message[/undefined method 'shout'/]
end

r = using Loud
p r.equal?(self)
p g.hi, g.shout, "abc".shout
p "x".then { |s| s.shout }

# a refinement in a class body and its methods in the methods of the class
class User
  using Quiet
  def greet(x) = x.hi
end
p User.new.greet(g)

# send, respond_to? and method see the refinement; the class does not list it
p g.respond_to?(:shout), g.respond_to?(:nope)
p g.send(:shout), g.public_send(:hi), g.__send__(:name)
p Greeter.instance_methods(false).sort
p Greeter.new.method(:hi).call, g.method(:shout).call
def helper(x) = x.hi
p helper(g)

# the refinement value and the errors
module Spy
  r = refine(Integer) { def double = self * 2 }
  p r.is_a?(Module), r.to_s[/#<refinement:Integer@/]
end
using Spy
p 21.double, (2 + 3).double

begin
  Module.new { refine(1) {} }
rescue TypeError => e
  puts e.message
end
begin
  using 1
rescue TypeError => e
  puts e.message
end
begin
  using Class.new
rescue TypeError => e
  puts e.message
end
def in_method = using(Loud)
begin
  in_method
rescue RuntimeError => e
  puts e.message
end
