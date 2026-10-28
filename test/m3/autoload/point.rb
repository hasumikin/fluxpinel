puts "loading point"

module Geo
  class Point
    def initialize(x, y)
      @x = x
      @y = y
    end

    def to_a = [@x, @y]
  end
end
