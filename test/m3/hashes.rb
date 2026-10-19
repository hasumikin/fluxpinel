# Hash: literals, access, defaults, the Enumerable methods and keywords.

h = { "a" => 1, b: 2, 3 => [4] }
p h
p h["a"], h[:b], h[3], h[:none]
h[:c] = 5
p h.size
p h.keys
p h.values
p h.key?(:b), h.include?(:zz)
p h.fetch(:b)
p h.fetch(:zz, 0)
p h.fetch(:zz) { |k| k.to_s * 2 }
p h.delete("a")
p h.delete(:none)
p h
p({}.empty?)
x = 7
p({ x: })

# a String key is copied: changing the String later does not change the key
s = +"key"
g = { s => 1 }
s << "!"
p g, g["key"]

# defaults
d = Hash.new(0)
"hello world".each_char { |c| d[c] += 1 }
p d["l"], d["z"]
lists = Hash.new { |hash, k| hash[k] = [] }
lists[:a] << 1
lists[:a] << 2
lists[:b] << 3
p lists
memo = {}
memo[:x] ||= 10
memo[:x] ||= 20
p memo

# iteration
scores = { alice: 3, bob: 5, carol: 4 }
scores.each { |name, n| puts "#{name}: #{n}" }
scores.each_pair { |name, n| print name, n }
puts
scores.each_with_index { |(name, n), i| p [i, name, n] }
p scores.map { |name, n| n * 2 }
p scores.select { |_, n| n > 3 }
p scores.reject { |_, n| n > 3 }
p scores.min_by { |_, n| n }
p scores.max_by { |_, n| n }
p scores.sort_by { |name, n| -n }
p scores.sum { |_, n| n }
p scores.count { |_, n| n.odd? }
p scores.find { |_, n| n == 4 }
p scores.any? { |_, n| n > 4 }
p scores.to_a
p scores.transform_values { |n| n * 10 }
p scores.transform_keys(&:to_s)
p scores.each_with_object({}) { |(k, v), acc| acc[v] = k }
p scores.inject(0) { |acc, (_, v)| acc + v }
p scores.invert
p scores.key(5)
p scores.sort
scores.delete_if { |_, n| n < 4 }
p scores

# merge
a = { x: 1, y: 2 }
b = { y: 3, z: 4 }
p a.merge(b)
p a.merge(b) { |_, old, new| old + new }
a.update(b)
p a
p({ **a, w: 0 })
p a == { x: 1, y: 3, z: 4 }

# from Arrays
p %w[a b a c a].tally
p [[1, 2], [3, 4]].to_h
p [1, 2, 3].to_h { |v| [v, v * v] }
p (1..6).group_by(&:even?)
p [1, 2, 3, 4].group_by { |v| v % 3 }

# keyword arguments
def kw(a, b: 2, c:)
  [a, b, c]
end
p kw(1, c: 3)
p kw(1, b: 5, c: 3)
def kwrest(x, **opts)
  [x, opts]
end
p kwrest(1, y: 2, z: 3)
p kwrest(1)
def plain(opts)
  opts
end
p plain(k: 1)

# a user object as a key
class Pt
  attr_reader :x, :y

  def initialize(x, y)
    @x = x
    @y = y
  end

  def hash
    [x, y].hash
  end

  def eql?(o)
    o.x == x && o.y == y
  end
end
grid = { Pt.new(1, 2) => :here }
p grid[Pt.new(1, 2)]
p grid[Pt.new(2, 1)]

# splat, ... and anonymous forwarding
def three(a, b, c) = [a, b, c]
args = [1, 2]
p three(*args, 3)
p three(*[1, 2, 3])
def fwd(...) = three(...)
p fwd(4, 5, 6)
def lead(x, ...) = [x, three(...)]
p lead(0, 7, 8, 9)
def anon_rest(*) = three(*)
p anon_rest(1, 2, 3)
def kwopts(a, k: 0) = [a, k]
def anon_kw(**) = kwopts(1, **)
p anon_kw(k: 5)
p kwopts(1, **{})
def blk_fwd(&) = [1, 2].map(&)
p blk_fwd { |v| v * 10 }

# a Hash literal in the last place stays positional
pr = proc { |a, k: 1| [a, k] }
p pr.call(5, k: 2)
p pr.call(5, { k: 2 })

# a bare super passes the keywords
class KwBase
  def go(a, b: 1, **rest) = [a, b, rest]
end
class KwChild < KwBase
  def go(a, b: 2, **rest)
    super
  end
end
p KwChild.new.go(0, c: 3)
