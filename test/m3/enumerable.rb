# Enumerable on a class that defines each: the methods walk the values that
# each yields. first, take, find, include?, find_index and take_while stop
# the walk early, so they work on an each that never ends.

class Bag
  include Enumerable

  def initialize(*items) = @items = items

  def each
    @items.each { |x| yield x }
    self
  end
end

b = Bag.new(3, 1, 2)
p b.to_a, b.entries, b.map { |x| x * 2 }, b.select(&:odd?), b.reject(&:odd?), b.sort, b.min, b.max
p b.sum, b.count, b.count(1), b.inject(:+), b.reduce(10) { |s, x| s + x }, b.sort_by { -_1 }
p b.include?(2), b.member?(5), b.first, b.first(2), b.take(1), b.find { _1 > 1 }, b.find_index(2)
slices = []
b.each_slice(2) { slices << _1 }
p b.group_by(&:odd?), b.partition(&:even?), slices, b.each_with_index.to_a, b.zip(b.to_a)
p b.min_by { -_1 }, b.minmax, b.tally, b.to_h { [_1, _1 * 10] }, b.uniq, b.each_with_object([]) { |x, a| a << x }
p b.each_with_index { |x, i| }.class, b.any?(Integer), b.all? { _1 > 0 }, b.none?(String), b.filter_map { _1 * 3 if _1.odd? }

# an each that never ends: the early methods stop it
class Naturals
  include Enumerable

  def each
    n = 0
    loop { yield(n += 1) }
  end
end

nat = Naturals.new
p nat.first, nat.first(3), nat.take(2), nat.find { _1 % 7 == 0 }, nat.include?(5), nat.find_index(4),
  nat.take_while { _1 < 4 }

# each that yields two values gives an Array, as each_entry does
class Pairs
  include Enumerable

  def each
    yield :a, 1
    yield :b, 2
  end
end
p Pairs.new.to_a, Pairs.new.map { |k, v| "#{k}=#{v}" }, Pairs.new.to_h, Pairs.new.first

# a method of the class wins, and super reaches Enumerable
class Loud < Bag
  def map = super.map(&:to_s)
end
p Loud.new(1, 2).map { _1 + 1 }

# a break in the block of the call ends the call
p(b.map { |x| break :stopped if x == 1; x })
