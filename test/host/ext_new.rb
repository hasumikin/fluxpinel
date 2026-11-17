# `new` of a class of the kernel (test/kernel/app.rb): the table of
# --ext-class has a row of `new` for each count of arguments that the
# initialize of the class takes, so a script makes an object of the kernel.
# Only make test-host runs this file, and CRuby requires the kernel for its
# answer.

c = FxKernel::Counter.new("made")
p c.class, c.instance_of?(FxKernel::Counter), c.name, c.n
p c.bump(2), c.step, c.scale(2)
# the object is one of the kernel: an entry and a method with a block take it
p FxKernel.bump(c, 1)
seen = []
c.each_n { |i| seen << i }
p seen.size
# each new makes an object of its own
d = FxKernel::Counter.new("other")
p d.name, d.n, c.equal?(d)
# an initialize with more parameters, and its ivars
s = FxKernel::Shape.new("box", 4)
p s.class, s.instance_variable_get(:@name), s.instance_variable_get(:@w), s.instance_variable_get(:@ratio)
p s.grow, FxKernel.grow(s)
# a class with no initialize takes no argument
k = FxKernel::Keeper.new
p k.class, k.keep { |n| n * 3 }.fire(2)
# the arity of the initialize, and a count that it does not take
p FxKernel::Counter.respond_to?(:new), FxKernel::Counter.method(:new).arity, FxKernel::Shape.method(:new).arity
begin
  FxKernel::Counter.new
rescue ArgumentError => e
  p e.message
end
begin
  FxKernel::Keeper.new(1)
rescue ArgumentError => e
  p e.message
end
# new through send, and on a class that a variable holds
klass = FxKernel::Counter
p klass.new("var").name, klass.send(:new, "sent").name, klass.public_send(:new, "pub").name
