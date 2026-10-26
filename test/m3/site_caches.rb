# The ivar caches of the nodes and of the accessors see each class, and
# the locals of the nested frames stay right

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
