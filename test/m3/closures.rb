# Blocks that outlive their scope: the frame moves to the heap.

def counter
  n = 0
  -> { n += 1 }
end
c = counter
p c.call, c.call, c.call
d = counter
p d.call, c.call

# two Procs of one scope share its variables, and the scope sees them too
def pair
  v = 10
  get = -> { v }
  set = ->(x) { v = x }
  set.call(5)
  [get, set, v]
end
r = pair
p r[2], r[0].call
r[1].call(42)
p r[0].call

# a block in a block: the outer block frame moves as well
adders = [1, 2, 3].map { |i| ->(x) { x + i } }
p adders.map { |a| a.call(10) }

# the variable moves while its scope runs on
def later
  x = 1
  pr = proc { x * 100 }
  x = 2
  [pr, x]
end
r = later
p r[0].call, r[1]

# x op= with a Proc in the operand, and x ||= one
def ops
  acc = []
  acc += [-> { acc.size }]
  f = nil
  f ||= -> { f.class }
  [acc[0].call, f.call]
end
p ops

# a default argument that makes a Proc
def with_default(a = 3, k: -> { a * 2 })
  k
end
p with_default.call, with_default(5).call

# &blk kept, and yield in a Proc after the method returned
def keep(&blk) = blk
def yielder = proc { yield + 1 }
k = keep { |y| y * 3 }
p k.call(4), yielder { 41 }.call

# return and break from a Proc whose method has returned
def returner = proc { return 1 }
begin
  returner.call
rescue LocalJumpError => e
  puts e.message
end
def breaker = proc { break 2 }
begin
  breaker.call
rescue LocalJumpError => e
  puts e.message
end

# return in a Proc while its method runs leaves that method
def early
  [1, 2, 3].each(&proc { |i| return i * 10 if i == 2 })
  :never
end
p early

# threads made in a block that returns before they run
ts = (1..4).map { |i| Thread.new { i * i } }
p ts.map(&:value)
ths = 3.times.map { |i| Thread.new(i) { |n| n + 100 } }
p ths.map(&:value)

# a Fiber made in a method that returns
def make_fiber
  base = 7
  Fiber.new { Fiber.yield base; base + 1 }
end
fb = make_fiber
p fb.resume, fb.resume

# many closures and the collector
procs = (1..200).map { |i| s = "v#{i}"; -> { s * 2 } }
junk = (1..20_000).map { |i| "junk#{i}" }
p procs[0].call, procs[199].call, junk.size
