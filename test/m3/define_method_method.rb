# define_method(name, method): the body of a Method or an UnboundMethod
# runs on the receiver of the new method; the method must come from an
# ancestor of the class.
def helper(x) = "#{self.class}:#{x * 2}"
class K
  def own(y) = "own #{y} of #{self.class}"
  def block_body = [1, 2].map { |i| i + size }
  def size = 10
end
class L < K
  def size = 100
end
class M; end

p K.define_method(:h, method(:helper))
p K.new.h(2), K.new.method(:h).arity
L.define_method(:o2, K.instance_method(:own))
p L.new.o2(3)
L.define_method(:bb, K.instance_method(:block_body))
p L.new.bb
M.define_method(:h3, method(:helper))
p M.new.h3(4), M.new.respond_to?(:h3), M.instance_methods(false).sort
begin
  M.define_method(:bad, K.instance_method(:own))
rescue TypeError => e
  p e.message
end
k = K.new
def k.sing(z) = "sing #{z}"
begin
  M.define_method(:s, k.method(:sing))
rescue TypeError => e
  p e.message
end
p M.new.respond_to?(:s)

# a singleton class takes one too
o = Object.new
o.singleton_class.define_method(:h4, method(:helper))
p o.h4(5)

# a method of a module binds to any class; a class method binds to the
# singleton side of its own class or of a subclass alone; a Method of a
# builtin binds to its class or a subclass
module Mod
  def whoami = self.class
end
class Plain
  def self.cm = :cm
end
Plain.define_method(:who, Mod.instance_method(:whoami))
p Plain.new.who
Plain.define_singleton_method(:who, Mod.instance_method(:whoami))
p Plain.who
begin
  Plain.define_method(:cm2, Plain.method(:cm))
rescue TypeError => e
  p e.message
end
Plain.define_singleton_method(:cm3, Plain.method(:cm))
p Plain.cm3
class Sub < Plain; end
Sub.define_singleton_method(:cm4, Plain.method(:cm))
p Sub.cm4
begin
  Plain.define_method(:up, "x".method(:upcase))
rescue TypeError => e
  p e.message
end
class Str < String; end
Str.define_method(:up, "x".method(:upcase))
p Str.new("ab").up

# the copy belongs to the class it is defined in: super goes on from
# there, under the new name too, and the constants of the body stay those
# of where it was written
class Base
  def f = :base
end
class Mid < Base
  WHERE = :mid
  def f = [WHERE, super()]
end
class Leaf < Mid
  WHERE = :leaf
end
Leaf.define_method(:f, Mid.instance_method(:f))
p Leaf.new.f, Leaf.instance_method(:f).owner
Leaf.define_method(:g, Mid.instance_method(:f))
p Leaf.new.g
