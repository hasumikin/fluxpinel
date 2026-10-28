# A subclass of Array: its instance is an Array with the methods of the
# class, super into Array, ivars, the constructor forms, dup and to_a.

module Countable
  def twice = size * 2
end

class Names < Array
  include Countable

  def initialize(*names)
    super(names)
    @tag = "names"
  end
  attr_reader :tag

  def shout = map(&:upcase)
  def second = self[1]
  def <<(name) = super(name.downcase)
  def first(n = 1) = super(n).reverse

  def each_initial
    each { |n| yield n[0] }
  end
end

class Short < Names
  def short? = size < 3
end

class Bag < Array; end

n = Names.new("Ann", "Bob")
n << "CAROL"
p n, n.class, n.tag, n.shout, n.second, n.twice
p n.first(2), n.last, n.size, n.include?("Bob"), n.index("carol")
n.each_initial { |i| print i }
puts
p n.drop(1).class, n.select { |x| x.size == 3 }.class, n.sort.class, (n + ["x"]).class
p n.to_a.class, n.to_a == n, n.map(&:size)
p n.push("dan").equal?(n), n.size
p n.is_a?(Array), n.is_a?(Names), n.instance_of?(Array), Names.ancestors.include?(Array)
d = n.dup
p d.class, d.tag, d == n, d.equal?(n)
p n.dup.push("x").size, n.size
s = Short.new("a")
p s.short?, s.class, s.tag
p Bag.new.class, Bag.new(2).size, Bag.new(2, 0), Bag.new([1, 2]).class, Bag.new(3) { |i| i * i }
p Bag[1, 2].class, Bag[1, 2]
p Bag.new == [], Bag[1] == [1], [1] == Bag[1], n.inspect == n.to_a.inspect
p n.respond_to?(:shout), n.respond_to?(:push), n.frozen?
h = { n => 1 }
p h[n]
b = Bag[3, 1, 2]
b.sort!
p b, b.class, b.reverse.class, b.instance_variables
b.instance_variable_set(:@x, 1)
p b.instance_variables, b.instance_variable_get(:@x)
