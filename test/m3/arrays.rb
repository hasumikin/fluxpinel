# The methods of Array that spinel emits from codegen code (array.c), on
# a poly array and on a typed one (the String Array of split).

a = [3, 1, 2]
p a.empty?, [].empty?, a.count, a.sort, a, a.min, a.max, a.minmax
p a.pop, a, a.shift, a, a.pop, a.pop, a.shift
b = [1, 2, 2, 3, 1]
p b.uniq, b.index(2), b.rindex(2), b.index(9), b.count(2), b.tally
p [1, [2, [3, [4]]]].flatten, [1, [2, [3, [4]]]].flatten(1)
p [[1, 2], [3, 4], [5, 6]].transpose
begin
  [[1, 2], [3]].transpose
rescue IndexError => e
  puts e.message
end
c = [5, 4]
c.push(3, 2)
c.append(1)
c.unshift(9, 8)
c.prepend(7)
c.insert(2, :x, :y)
p c
c.concat([10], [11, 12])
c.delete(:x)
c.delete(:y)
p c, c.sort!, c
p [1, 2] | [2, 3], [1, 2, 2] & [2], [1, 2].union([3], [1, 4]), [1, 2, 3].difference([2], [3])
p [1, 2, 3].intersection([2, 3, 4]), [1, 2].intersect?([2]), [1, 2].intersect?([5])
p [1, 2, 3][1..], [1, 2, 3][-2..], [1, 2, 3].slice(0..1), [1, 2, 3].at(-1), [1, 2, 3].member?(2)
d = [1, 2, 3, 4, 5]
d[1..2] = [:a]
p d
d[0, 2] = [7, 8, 9]
p d
p d.fetch(1), d.fetch(-1), d.fetch(10, :none), d.fetch(10) { |i| i * 2 }
begin
  d.fetch(10)
rescue IndexError => e
  puts e.message
end
p [[1, :a], [2, :b]].assoc(2), [[1, :a], [2, :b]].rassoc(:a), [[1, :a]].assoc(3)
e = [1, nil, 2, nil]
p e.delete(nil), e, e.delete(9), e.compact!, [[1, [2]]].flatten!, [1].flatten!
p [[1, [2, 3]]].dig(0, 1, 0), [1, 2, 3, 4].values_at(0, 2, 9)
p [1, 2].zip([3, 4], [5]), [1, 2].product([3, 4]), [1, 2, 3].sum(10), [3, 1, 2].min(2), [3, 1, 2].max(2)
f = [1, 2, 3]
f.fill(0)
p f, [1, 2, 3].fill(9, 1), [1, 2, 3, 4].fill(7, 1, 2)
g = [1, 2, 3]
p g.pop(2), g, [1, 2, 3].shift(2), [3, 4].replace([5]), [1, 2, 3].sample(5).size
h = "a b c".split(" ")
h.push("d")
h.unshift("z")
p h, h.pop, h.sort, h.uniq
begin
  [1].freeze.pop
rescue => e
  p e.class
end
begin
  [1] | 1
rescue TypeError => e
  puts e.message
end
p [1, 2].respond_to?(:pop), [].pop, [].shift, [].min

# the methods of the program that the helpers call: <=>, to_int, to_ary
class Money
  include Comparable
  attr_reader :n
  def initialize(n) = @n = n
  def <=>(o) = n <=> o.n
  def inspect = "$#{n}"
end
m = [Money.new(3), Money.new(1), Money.new(2)]
p m.sort, m.min, m.max, m.minmax
class Idx
  def to_int = 1
end
p [10, 20, 30][Idx.new], [10, 20].fetch(Idx.new)
class Pair
  def to_ary = [7, 8]
end
s = [1, 2, 3]
s[1, 1] = Pair.new
p s, [1, 2].concat(Pair.new), [1, 2].zip(5..), [1, 2].zip(1..1)

# Array.new: the same value in each place, a block per index, or a copy
a = Array.new(2, "x")
a[0] << "y"
src = [1, 2]
c = Array.new(src)
c << 3
p Array.new, Array.new(3), a, Array.new(4) { |i| i * i }, src, c
p Array.new(3) { |i| break :out if i == 1; i }
p((Array.new(-1) rescue $!.message), (Array.new("x") rescue $!.class))

# a splat in an Array literal spreads an Array, the to_a of a value that
# has one, nothing for nil, and any other value as one element
xs = [2, 3]
Pt = Struct.new(:x, :y)
class Bag
  def to_a = [:a, :b]
end
class Bad
  def to_a = 1
end
p [1, *xs, 4], [*nil], [*1..3], [*"ab"], [*:s], [*{ k: 1, j: 2 }], [*Pt.new(5, 6)], [*Bag.new]
p [*"a".."c"], [*1.0], [*xs, *xs], [*[1, 2].each], [0, *[], 9]
def anon(*) = [0, *, 9]
p anon(1, 2)
def take(*args) = args
p take(*{ a: 1 }), take(*Bag.new), take(*"s"), take(*nil)
case :b
when *Bag.new then p :hit
end
p(([*Bad.new] rescue [$!.class, $!.message]))

# return, next and break carry the values that a splat spreads
def ret_splat = (return *xs_of(2))
def xs_of(n) = (1..n).to_a
x = *nil
p ret_splat, [1, 2].map { |v| next *[v, v] }, [1].each { break *{ k: 1 } }, x
