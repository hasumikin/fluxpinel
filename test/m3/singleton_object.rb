# The singleton class of an object: def obj.m, class << obj, extend,
# define_singleton_method, and the lookup of its methods.

class Foo
  def initialize(n) = @n = n
  attr_reader :n
  def hi = "hi #{@n}"
end

a = Foo.new(1)
b = Foo.new(2)
def a.hi = "special #{super}"
def a.only = :only
p a.hi, b.hi, a.only, a.respond_to?(:only), b.respond_to?(:only)
p a.class, a.is_a?(Foo), a.instance_of?(Foo), a.singleton_methods.sort, b.singleton_methods
p a.method(:only).owner == a.singleton_class, a.singleton_class.inspect.start_with?("#<Class:#<Foo:0x")
p a.singleton_class.instance_methods(false).sort, a.singleton_class.superclass

class << a
  def shout = hi.upcase
  attr_accessor :extra
end
a.extra = 5
p a.shout, a.extra, b.respond_to?(:shout), a.singleton_methods.sort

module Loud
  def loud = "LOUD #{n}"
end
b.extend(Loud)
p b.loud, a.respond_to?(:loud), b.singleton_class.include?(Loud), b.is_a?(Loud), Loud === b
b.define_singleton_method(:twice) { n * 2 }
p b.twice, b.singleton_methods.sort, b.singleton_methods(false)
def b.n = 99
p b.n, b.twice, Foo.new(3).n

h = { k: 1 }
def h.inspect = "CONF"
p h.inspect, h.size, h.class, h[:k]

o = Object.new
def o.to_s = "custom"
p o.to_s, "#{o}", o.respond_to?(:to_s)

module Ext
  def self.extended(base) = puts("extended #{base.class}")
  def ext = :ext
end
x = Foo.new(4).extend(Ext)
p x.ext, x.class

one = 1
begin
  def one.odd = true
rescue TypeError => e
  puts e.message
end
begin
  :sym.singleton_class
rescue TypeError => e
  puts e.message
end
