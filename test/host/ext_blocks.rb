# A block of the script for a method of the kernel (test/kernel/app.rb):
# the table of --ext-class has the methods that take a block, and the
# kernel calls the block as a Proc of the runtime. Only make test-host runs
# this file, and CRuby requires the kernel for its answer.

c = FxKernel.make("c")
c.bump(3)
seen = []
p c.each_n { |i| seen << i }.equal?(c), seen
p c.method(:each_n).arity, c.method(:with_n).arity
# a block parameter after an optional parameter, and the forms of a block
p c.with_n { |v| v * 2 }, c.with_n(10) { |v| v * 2 }
p c.with_n(&:to_s), c.with_n(&->(v) { v + 1 }), c.with_n(&c.method(:scale))
# a method that takes no block does not use it
p c.bump(1) { :unused }
# next gives the value of the block
p c.with_n { |v| next v + 100; 0 }
# break ends the call with its value, and the ensure clause of the kernel
# runs
p c.each_n { |i| break i * 10 if i == 2 }
p c.each_n { |i| break if i == 2 }
p c.guarded { |n| break n }, c.n
# a return in the block ends the method of the script
def first_over(c, k)
  c.each_n { |i| return i if i > k }
  :none
end
p first_over(c, 1), first_over(c, 1000)
def guarded_return(c)
  c.guarded { |n| return n }
end
p guarded_return(c), c.n
# a call in a block of another call: each break ends its own call
d = FxKernel.make("d")
d.bump(3)
out = []
r = d.each_n do |i|
  d.each_n { |j| break if j > i; out << [i, j] }
  break :outer if i == 1
end
p r, out
# a raise in the block passes the kernel, and its ensure clause runs
begin
  d.each_n { |i| raise ArgumentError, "at #{i}" }
rescue ArgumentError => e
  p e.message
end
before = d.n
begin
  d.guarded { raise "in guarded" }
rescue => e
  p e.message, d.n - before
end
# a throw in the block reaches its catch
p catch(:done) { d.each_n { |i| throw :done, i + 1 if i == 1 }; :no }
before = d.n
p catch(:done) { d.guarded { |n| throw :done, :thrown } }, d.n - before
# with no block, the yield of the kernel raises
begin
  d.each_n
rescue LocalJumpError => e
  p e.class
end
# a block that the kernel keeps: it runs after the call, and a break in it
# has no call to end
k = FxKernel::Keeper.make
p k.keep { |n| n * 3 }.fire(2)
k.keep { |n| break n }
begin
  k.fire(2)
rescue LocalJumpError => e
  p e.message
end
# a block that the kernel calls in another thread: the same
p k.in_thread { |n| n + 10 }
begin
  k.in_thread { |n| break n }
rescue LocalJumpError => e
  p e.message
end
# the threads of the script call with blocks at the same time
e = FxKernel.make("e")
e.bump(50)
ts = [1, 2, 3].map do |m|
  Thread.new do
    s = 0
    e.each_n { |i| s += i * m }
    s + e.each_n { |i| break i * m if i == 40 }
  end
end
p ts.map(&:value)
