class Version
  include Comparable
  attr_reader :major, :minor

  def initialize(major, minor = 0)
    @major = major
    @minor = minor
  end

  def <=>(other)
    [major, minor] <=> [other.major, other.minor] if other.is_a?(Version)
  end

  def to_s
    "#{major}.#{minor}"
  end
end

a = Version.new(1, 2)
b = Version.new(1, 10)
p a < b, a >= b, a == Version.new(1, 2), a.between?(Version.new(1), b)
puts a.clamp(b, Version.new(2))

class Money
  attr_reader :cents

  def initialize(cents)
    @cents = cents
  end

  def *(n)
    Money.new(cents * n)
  end

  def coerce(n)
    [self, n]
  end

  def to_s
    "$#{cents / 100}.#{cents % 100}"
  end
end

puts Money.new(250) * 3
puts 3 * Money.new(250)

class Base
  @subclasses = []
  class << self
    attr_reader :subclasses
  end

  def self.inherited(sub)
    @subclasses << sub.name
  end
end

class Left < Base; end
class Right < Base; end
p Base.subclasses

module Tracked
  def self.included(base)
    puts "Tracked included in #{base}"
  end
end

class Widget
  include Tracked
end

class Shape
  def initialize(name, sides = 0)
    @name = name
    @sides = sides
  end

  def describe
    "#{@name}/#{@sides}"
  end
end

class Square < Shape
  def initialize(name, sides = 4)
    super
  end
end

puts Square.new("sq").describe
p Square < Shape, Shape < Square, Square <= Square, Integer < Shape
