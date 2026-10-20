# Struct, Data and anonymous classes.

def t
  yield
rescue => e
  puts "#{e.class}: #{e.message}"
end

Point = Struct.new(:x, :y) do
  def dist2
    x * x + y * y
  end

  def to_s
    "(#{x}, #{y})"
  end
end

a = Point.new(3, 4)
p a
puts a
p a.dist2, a.x, a[1], a[:x], a["y"], a[-1]
a.x = 10
a[:y] = 20
p a.to_a, a.to_h, a.members, Point.members, a.size
p a == Point.new(10, 20), a.eql?(Point.new(10, 20)), a == Point.new(10, 21)
p a.hash == Point.new(10, 20).hash
p Point.new(1), Point[5, 6]
p({ Point.new(1, 2) => :hit }[Point.new(1, 2)])
p [Point.new(1, 2)].include?(Point.new(1, 2))
a.each { |v| print v, " " }
puts
a.each_pair { |k, v| print k, "=", v, " " }
puts
p a.select(&:even?), a.map { |v| v * 2 }, a.sum
p a.to_h { |k, v| [k.to_s, v + 1] }
p a.values_at(0, 1), a.dig(:x)
t { Point.new(1, 2, 3) }
t { a[5] }
t { a[:z] }
t { a[-3] }

K = Struct.new(:name, :age, keyword_init: true)
k = K.new(name: "ann", age: 3)
p k, K.keyword_init?, Point.keyword_init?
t { K.new("ann", 3) }
t { K.new(nope: 1) }
p Point.new(x: 1, y: 2)

class Pair < Struct.new(:left, :right)
  def initialize(left, right = :none)
    super
  end

  def swap
    Pair.new(right, left)
  end
end
p Pair.new(1), Pair.new(1, 2).swap
p Pair.ancestors.include?(Struct)

Coord = Data.define(:lat, :lng) do
  def to_s
    "#{lat},#{lng}"
  end
end
c = Coord.new(lat: 1.5, lng: 2)
p c, Coord.new(3, 4)
puts c
p c.with(lng: 9), c.to_h, c.frozen?, c == Coord.new(1.5, 2)
t { Coord.new(1) }
t { Coord.new }
t { Coord.new(1, 2, 3) }
t { Coord.new(lat: 1, lng: 2, alt: 3) }
t { c.with(alt: 1) }
t { c.lat = 5 }

Money = Data.define(:amount, :currency) do
  def initialize(amount:, currency: "JPY")
    super
  end
end
p Money.new(amount: 100), Money.new(5, "USD")

p Coord.new(1, 2).deconstruct_keys([:lat]), Coord.new(1, 2).deconstruct, a.deconstruct_keys(nil)

anon = Struct.new(:q)
p anon.new(1), anon.name
Named = anon
p anon.new(2), anon.name

klass = Class.new do
  def hello
    "hi from #{self.class.name}"
  end
end
Greeter = klass
p Greeter.new.hello
Sub = Class.new(Greeter) do |c|
  $sub_arg = c
  def extra = :extra
end
p Sub.new.hello, Sub.new.extra, Sub.superclass, $sub_arg == Sub
m = Module.new do
  def mixed = :mixed
end
Mix = m
class Greeter
  include Mix
end
p Greeter.new.mixed
p Class.new.name
