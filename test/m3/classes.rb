class Point
  attr_reader :x, :y
  attr_accessor :label

  ORIGIN_NAME = "origin"

  def initialize(x, y = 0)
    @x = x
    @y = y
  end

  def +(other)
    Point.new(@x + other.x, @y + other.y)
  end

  def ==(other)
    other.is_a?(Point) && x == other.x && y == other.y
  end

  def to_s
    "(#{@x}, #{@y})"
  end

  def self.origin
    new(0, 0)
  end

  def origin?
    self == Point.origin
  end
end

a = Point.new(1, 2)
b = Point.new(3)
puts a, b
puts a + b
p a == Point.new(1, 2), a == b, a != b
p Point.origin.origin?, a.origin?
a.label = "A"
p a.label
p Point::ORIGIN_NAME
p a.is_a?(Point), a.is_a?(Object), 1.is_a?(Point), 1.is_a?(Integer)
p a.class, a.class.name, Point.superclass
p [a, b].include?(Point.new(3, 0))

class Point3 < Point
  attr_reader :z

  def initialize(x, y, z)
    super(x, y)
    @z = z
  end

  def to_s
    super + "+#{@z}"
  end
end

c = Point3.new(1, 2, 3)
puts c
p c.is_a?(Point), c.instance_of?(Point), Point3.superclass
p c.respond_to?(:z), a.respond_to?(:z)

class Point
  def norm1
    x.abs + y.abs
  end
end

p c.norm1
p Point3.ancestors.include?(Point)

class Counter
  @count = 0
  class << self
    attr_accessor :count
  end

  def self.bump
    @count += 1
  end
end

Counter.bump
Counter.bump
p Counter.count
p a.send(:+, b).to_s
p a.instance_variable_get(:@x)
a.instance_variable_set(:@x, 10)
p a.x
p a.instance_variables
