# Multiple assignment: the targets, the splat, nesting, to_ary and the
# order of evaluation.

a, b = 1, 2
p [a, b]
a, b = b, a
p [a, b]
x, y = [3, 4, 5]
p [x, y]
x, *y = 1, 2, 3
p [x, y]
*x, y = 1, 2, 3
p [x, y]
x, *y, z = 1
p [x, y, z]
x, (y, z), w = 1, [2, 3], 4
p [x, y, z, w]
x, (y, *z) = 1, [2, 3, 4]
p [x, y, z]
x, = [7, 8]
p x
x, * = 9, 10
p x
x, y = nil
p [x, y]
x, y = "s"
p [x, y]
p((x, y = 1, 2))
p((x, y = [5, 6]))

class Pair
  def to_ary = [:l, :r]
end
x, y = Pair.new
p [x, y]
class Bad
  def to_ary = 1
end
begin
  x, y = Bad.new
rescue TypeError => e
  puts e.message
end
class NilAry
  def to_ary = nil
end
x, y = NilAry.new
p [x.class, y]

class Box
  attr_accessor :v, :w
end
$log = []
def t(tag, v)
  $log << tag
  v
end
bx = Box.new
h = {}
t(:r1, bx).v, t(:r2, h)[t(:k, :key)], @iv, $gv = t(:value, [1, 2, 3, 4])
p bx.v, h, @iv, $gv, $log
arr = [0, 0, 0]
arr[0], arr[2] = 8, 9
p arr
Z1, Z2 = 1, 2
p Z1 + Z2
module M; end
M::Q, w = :q, :w
p M::Q, w
def swap(a, b)
  a, b = b, a
  [a, b]
end
p swap(1, 2)
[[1, [2, 3]]].each { |i| q, (r, s) = i; p [q, r, s] }
