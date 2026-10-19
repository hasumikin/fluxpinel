# Blocks: yield, closures, block parameters and the jumps out of a block.

def twice
  yield 1
  yield 2
end

twice { |x| puts x * 10 }

def given?
  block_given?
end
p given?
p(given? { 1 })

# a block reads and writes the locals of its scope
sum = 0
[1, 2, 3].each { |x| sum += x }
p sum

def counter
  n = 0
  3.times { n += 1; [1].each { n += 10 } }
  n
end
p counter

# the block parameters of a Proc spread one Array argument
[[1, 2], [3, 4]].each { |a, b| p [b, a] }
[[1, [2, 3]]].each { |a, (b, c)| p [a, b, c] }
[[1, 2, 3]].each { |a, *r| p [a, r] }
[[1, 2]].each { |a| p a }
[[1, 2]].each { |a, | p a }
def pair
  yield 1
end
pair { |a, b| p [a, b] }
[5, 6].each_with_index { |v, i| p [v, i] }

# numbered parameters and it
p [1, 2, 3].map { _1 * 2 }
p [[1, 2], [3, 4]].map { _1 + _2 }
p [4, 5].map { it + 1 }

# next ends the block call; break ends the call that passed the block
p [1, 2, 3, 4].map { |x| next 0 if x.even?; x }
r = [1, 2, 3, 4].each { |x| break x * 100 if x == 3 }
p r
def find_first(a)
  a.each { |x| return x if x > 1 }
  nil
end
p find_first([1, 5, 7])
p find_first([0])
i = 0
v = loop do
  i += 1
  break i * 2 if i == 5
end
p v
while true
  [1, 2].each { |x| break }
  break
end
puts "after while"

# &blk, &:sym and forwarding
def capture(&b)
  b
end
pr = capture { |x| x + 1 }
p pr.call(1)
p pr.(2)
p pr[3]
p pr.lambda?
def fwd(a, &b)
  a.map(&b)
end
p fwd([1, 2]) { |x| x * 3 }
p [1, 2, 3].map(&:to_s)
p %w[a b].map(&:upcase)
def anon(&) = [1, 2].map(&)
p anon { |x| -x }

# proc and lambda
sq = lambda { |x| x * x }
p sq.call(4)
p sq.lambda?
add = ->(a, b) { a + b }
p add.(1, 2)
p add.arity
p proc { |a, b| }.arity
p proc { |*a| }.arity
lam = -> { return 5; 6 }
p lam.call
def lam_ret
  l = -> { return 1 }
  l.call
  2
end
p lam_ret
def proc_ret
  pr = proc { return 1 }
  pr.call
  2
end
p proc_ret
k = 10
adder = proc { |x| x + k }
k = 20
p adder.call(1)

# yield inside a block yields to the method's block
def outer
  [1, 2].each { |x| yield x * 2 }
end
outer { |y| p y }

# super passes the block
class Base
  def run
    yield :base
  end
end
class Child < Base
  def run
    super
  end
end
Child.new.run { |s| p s }

# a block passed to new reaches initialize
class Holder
  def initialize(&b)
    @b = b
  end

  def go
    @b.call(7)
  end
end
p Holder.new { |x| x * 2 }.go
