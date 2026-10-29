# Marshal.dump and Marshal.load: the builtin values, objects of the
# program, subclasses of Array and Hash, shared references and errors.

class Point
  attr_reader :x, :y
  def initialize(x, y)
    @x = x
    @y = y
  end
  def ==(o) = o.is_a?(Point) && x == o.x && y == o.y
end

class Tree
  attr_accessor :left, :right, :tag
  def initialize(tag) = @tag = tag
end

module Shapes
  class Box
    attr_reader :points
    def initialize(*points) = @points = points
  end
end

class Stack < Array
  attr_accessor :name
end

class Registry < Hash
  attr_accessor :owner
end

def roundtrip(v) = Marshal.load(Marshal.dump(v))

p roundtrip(nil), roundtrip(true), roundtrip(false), roundtrip(0), roundtrip(-1), roundtrip(2**40), roundtrip(-2**40)
p roundtrip(1.5), roundtrip("text"), roundtrip("text".b).encoding, roundtrip(:sym), roundtrip(1..3), roundtrip(1...3)
p roundtrip([1, "two", :three, [4.0, nil]]), roundtrip({ a: 1, "b" => [2], 3 => { c: nil } })
p roundtrip(Complex(1, 2)), roundtrip(Complex(1.5, 2)), roundtrip(Rational(3, 4))

s = Marshal.dump("abc")
p s.encoding, s.bytes.first(2), s.bytesize
p Marshal.dump([1, 2]).bytes
p Marshal.dump(Point.new(1, 2)).bytes

pt = roundtrip(Point.new(1, 2))
p pt.class, pt.x, pt.y, pt == Point.new(1, 2)
box = roundtrip(Shapes::Box.new(Point.new(0, 0), Point.new(3, 4)))
p box.class, box.points.map { |q| [q.x, q.y] }

# shared and cyclic references
a = [1, 2]
pair = roundtrip([a, a])
p pair[0].equal?(pair[1])
t = Tree.new(:root)
t.left = Tree.new(:l)
t.right = t.left
t.left.left = t
t2 = roundtrip(t)
p t2.tag, t2.left.tag, t2.left.equal?(t2.right), t2.left.left.equal?(t2)

# an object with no ivar set yet
p roundtrip(Tree.allocate).tag, roundtrip(Tree.allocate).instance_variables

# subclasses of Array and Hash
st = Stack.new
st << 1 << 2
st.name = "s"
st2 = roundtrip(st)
p st2.class, st2.to_a, st2.name
plain = Stack.new
plain << 9
p roundtrip(plain).class, roundtrip(plain).to_a, roundtrip(plain).name
rg = Registry.new
rg[:k] = "v"
rg.owner = :me
rg2 = roundtrip(rg)
p rg2.class, rg2.to_h, rg2.owner

# dump to an IO-like object, load from one
require "stringio"
io = StringIO.new
p Marshal.dump([1, :a], io).equal?(io)
io.rewind
p Marshal.load(io)
p Marshal.restore(Marshal.dump({ x: 1 }))
p Marshal.load(Marshal.dump("frozen"), freeze: false)

# the values that no dump takes
[proc {}, Object.new.singleton_class, Class.new.new, method(:p), binding].each do |v|
  begin
    Marshal.dump(v)
    puts "dumped #{v.class}"
  rescue TypeError => e
    puts e.message.start_with?("can't dump anonymous class") ? e.message.sub(/0x\h+/, "0x") : "TypeError"
  end
end
begin
  Marshal.load("\x04\x08o:\x09Nope\x00")
rescue ArgumentError => e
  puts e.class
end
begin
  Marshal.dump(1, 2, 3, 4)
rescue ArgumentError => e
  puts e.message
end
p Marshal.respond_to?(:dump), Marshal.respond_to?(:load), Marshal.respond_to?(:nope), Marshal.class, Marshal::MAJOR_VERSION
