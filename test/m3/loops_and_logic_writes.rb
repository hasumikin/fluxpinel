# for, begin...end while, redo, the logic writes on locals, ivars,
# attributes and constants, and interpolated Symbols.

# for: the index and the body locals stay in the scope
for i in 1..3
  last = i * 10
end
p [i, last]
p(for x in [1, 2, 3] do end)
for a, b in [[1, :x], [2, :y]]
  p [a, b]
end
for k, v in { p: 1, q: 2 }
  p [k, v]
end
p(for n in [1, 2, 3, 4]
    next if n.odd?
    break n * 100 if n == 4
  end)
class Box
  attr_accessor :item
end
box = Box.new
for box.item in %w[a b] do end
p box.item
for @iv in [7, 8] do end
p @iv
r = []
[1, 2].each { |e| for y in [e, e + 10] do r << y end }
p r
class Countdown
  include Enumerable
  def each
    yield 3
    yield 2
    yield 1
    :done
  end
end
p(for c in Countdown.new do end)
def first_big(list)
  for e in list
    return e if e > 5
  end
  nil
end
p first_big([1, 6, 9])

# begin...end while and until run the body once before the test
n = 0
begin
  n += 1
end while n < 0
p n
m = 10
begin
  m -= 3
end until m < 0
p m
k = 0
got = begin
  k += 1
  break k * 2 if k == 3
end while true
p got

# redo runs the body again with the same value
tries = 0
out = [1, 2].map do |e|
  tries += 1
  redo if tries == 1
  e * tries
end
p [out, tries]
w = 0
seen = []
while w < 2
  w += 1
  seen << w
  redo if seen.size == 1
end
p seen
fr = []
for q in [5, 6]
  fr << q
  redo if fr.size == 1
end
p fr

# x &&= v on locals and ivars
l1 = 3
l1 &&= l1 + 1
l2 = nil
l2 &&= raise("not reached")
p [l1, l2]
@a = "s"
@a &&= @a + "t"
@b &&= 1
p [@a, @b]

# x.a ||= v and x.a &&= v
class Pt
  attr_accessor :x, :y
  def calls = (@calls ||= [])
  def z = (calls << :z; @z)
  def z=(v)
    calls << :z=
    @z = v
  end
end
pt = Pt.new
p(pt.x ||= 4)
p(pt.x ||= 5)
p(pt.y &&= 1)
pt.y = 2
p(pt.y &&= pt.y * 3)
pt.z ||= 1
pt.z ||= 2
p pt.calls
none = nil
p(none&.x ||= 1)
p(pt&.x ||= 9)

# constants
LIMIT = 3
LIMIT += 1
p LIMIT
module Conf; end
Conf::SIZE ||= 10
Conf::SIZE ||= 20
Conf::NAME = "a"
Conf::NAME &&= Conf::NAME + "b"
p [Conf::SIZE, Conf::NAME]
FRESH ||= :new
p FRESH
begin
  MISSING &&= 1
rescue NameError => e
  p e.class
end

# interpolated Symbols
nm = "key"
p :"#{nm}_1"
p %I[a#{1 + 1} b]
h = { "#{nm}": 1 }
p h
