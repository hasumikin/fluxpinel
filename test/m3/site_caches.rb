# The ivar caches of the nodes and of the accessors see each class, the
# method caches of the call nodes see each class and each change, and the
# locals of the nested frames stay right

# one @x site, objects of two classes with the slot of @x in other places
class P
  def initialize(x) = (@x = x)
  def x = @x
end
class Q
  def initialize(y, x)
    @y = y
    @x = x
  end
  def x = @x
end
class R < P
  def initialize(x)
    @z = :z
    super
  end
end
def read(o) = o.instance_variable_get(:@x)
[P.new(1), Q.new(2, 3), R.new(4), P.new(5)].each { |o| p o.x, read(o) }

# a read before the set, then after it
class Lazy
  def get = @v
  def put(v) = (@v = v)
end
l = Lazy.new
p l.get
l.put(7)
p l.get
p Lazy.new.get

# a write through the cache on a frozen object
class Box
  def initialize = (@v = 0)
  def bump = (@v += 1)
end
bx = Box.new
bx.bump
bx.freeze
begin
  bx.bump
rescue => e
  p e.class
end

# accessors on two classes
class S1
  attr_accessor :a, :b
end
class S2
  attr_accessor :b, :a
end
[S1.new, S2.new, S1.new].each_with_index do |o, i|
  o.a = i
  o.b = i * 10
  p [o.a, o.b]
end

# a class ivar and an object ivar at one site
class C
  def self.set = (@k = :class)
  def self.get = @k
  def set = (@k = :object)
  def get = @k
end
C.set
c = C.new
c.set
p C.get, c.get

# op-writes, or-writes and multiple targets
class W
  def run
    @n ||= 0
    @n += 2
    @a, @b = 1, 2
    @b &&= @b + 1
    [@n, @a, @b]
  end
end
w = W.new
p w.run, w.run

# one local node in blocks of other depths and in a define_method
def locals
  out = []
  3.times do |i|
    t = i
    [1].each { |j| t += j }
    out << t
  end
  out
end
p locals, locals
pr = proc { |v| s = v; s *= 2; s }
p pr.call(3), pr.call(4)

# one call site, receivers of other classes and of builtin classes
class Dog
  def speak = :woof
end
class Cat
  def speak = :meow
end
class Kitten < Cat
end
[Dog.new, Cat.new, Kitten.new, 3, Dog.new].each do |o|
  p(o.respond_to?(:speak) ? o.speak : o.to_s)
end

# a redefinition and an include between two calls at one site
class Cat
  def speak = :purr
end
module Loud
  def speak = :ROAR
end
def call_speak(o) = o.speak
k = Kitten.new
p call_speak(k)
class Kitten
  include Loud
end
p call_speak(k)

# a private and a protected method at one site, after a public call
class Guard
  def open = :open
  def run(o) = o.peek
  def peek = :peek
  protected :peek
  def close = :close
  private :close
end
g = Guard.new
p g.open, g.run(Guard.new)
[:open, :close, :peek].each do |m|
  begin
    p g.public_send(m)
  rescue NoMethodError => e
    p e.message[/private|protected/]
  end
end
def visit(o) = o.close
begin
  visit(g)
rescue NoMethodError => e
  p e.message[/private/]
end

# a self call in a method of two classes, and method_missing
class Base
  def go = step
  def step = :base
end
class Kid < Base
  def step = :kid
end
p Base.new.go, Kid.new.go, Base.new.go
class Ghost
  def method_missing(name, *args) = [:missing, name, *args]
  def respond_to_missing?(*) = true
  def try = boo(1)
end
p Ghost.new.try
class Ghost
  def boo(x) = [:real, x]
end
p Ghost.new.try

# a splat, a forward and a block at a call site
def sum3(a, b, c) = a + b + c
def fwd(...) = sum3(...)
args = [1, 2, 3]
p sum3(*args), fwd(4, 5, 6)
def yielder = yield(5)
def pass_on(&b) = yielder(&b)
p pass_on { |x| x * 2 }
