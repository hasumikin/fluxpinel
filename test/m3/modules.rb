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
