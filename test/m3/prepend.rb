# Module#prepend: a prepended module comes before the class in its
# ancestors, and its super reaches the method of the class. With it, the
# rules of CRuby for the place of a mixin.

module Loud
  def self.prepended(base) = puts("prepended to #{base}")
  def hello = "#{super.upcase}!"
end
module Polite
  def hello = "please, #{super}"
end
module Tag
  def hello = "<#{super}>"
end
class Greeter
  include Tag
  prepend Loud
  def hello = "hello"
end
p Greeter.new.hello, Greeter.ancestors.take(4)
class Greeter
  prepend Polite
end
p Greeter.new.hello, Greeter.ancestors.take(5), Greeter.include?(Loud), Greeter.new.is_a?(Polite), Greeter <= Loud
class Sub < Greeter
  def hello = "sub " + super
end
p Sub.new.hello, Sub.ancestors.take(6)

# a builtin class: a prepended module comes first, an included one after
# the own methods of the class
module Twice
  def to_s = "twice #{super}"
end
module Size
  def size = 99
  def extra = :extra
end
class Integer
  prepend Twice
end
class String
  include Size
end
p 5.to_s, "abc".size, "abc".extra

# a module that prepends another carries it, also into a singleton
module Inner; def m = "inner"; end
module Outer
  include Inner
  def m = "outer " + super
end
class Host
  prepend Outer
  def m = "host"
end
module Quiet
  def hi = "quiet"
  prepend(Module.new { def hi = "loud " + super })
end
class Extender
  extend Quiet
end
p Host.new.m, Host.ancestors.take(4), Extender.hi
class Two
  prepend Loud, Polite
  def hello = "two"
end
p Two.ancestors.take(3), Two.new.hello

# a module that an ancestor has already stays where it is
module M1; def a = 1; end
module M2; include M1; end
module M3; include M2; end
class A; include M2; include M3; include M1; end
p A.ancestors.take(4), A.include?(M1), M3.include?(M1), M1.include?(M2)

# a builtin module answers its names before a superclass of the program
class Base
  def count = 100
end
module ViaEnum
  include Enumerable
end
class Kid < Base
  include ViaEnum
  def each = (yield 5; yield 6)
end
class Eq
  def ==(other) = true
  include Comparable
end
p Kid.new.count, Eq.new == Eq.new, Eq.new <=> Eq.new
