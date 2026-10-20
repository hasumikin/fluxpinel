# A library file: a class, a constant and a local of its own.
local_here = :shapes_local
SHAPES_LOADED = [local_here]

class Square
  def initialize(side)
    @side = side
  end

  def area
    @side * @side
  end
end

require_relative "deep/util"
puts "shapes loaded"
