module Greeting
  PREFIX = "Hello"

  def greet
    "#{PREFIX}, #{name}!"
  end
end

module Loud
  def greet
    super.upcase
  end
end

class Person
  include Greeting
  attr_reader :name

  def initialize(name)
    @name = name
  end
end

class Shouter < Person
  include Loud
end

puts Person.new("Ann").greet
puts Shouter.new("Bob").greet
p Shouter.ancestors.first(4)
p Person.new("C").is_a?(Greeting)

module Util
  module_function

  def double(x)
    x * 2
  end
end

p Util.double(21)

module Outer
  VALUE = 1

  class Inner
    def value
      VALUE + 1
    end
  end

  module Deep
    NAME = "deep"
  end
end

p Outer::Inner.new.value
p Outer::Deep::NAME
p Outer::Inner.name
# the name is a frozen String; to_s and inspect answer a String to change
p Outer::Inner.name.frozen?, Outer::Inner.to_s.frozen?, Outer::Inner.inspect.frozen?, (Outer.name << "x" rescue $!.class)

module Helpers
  def self.twice(s)
    s * 2
  end
end

p Helpers.twice("ab")

class Integer
  def double
    self * 2
  end
end

p 21.double

LIMIT = 3
def under_limit?(n)
  n < LIMIT
end

p under_limit?(2), under_limit?(5)
