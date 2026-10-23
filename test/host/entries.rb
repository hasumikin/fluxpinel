# The entries of the kernel (test/kernel/app.rb), called from Fluxpinel. Only
# make test-host runs this file, and CRuby requires the kernel for its
# answer.

p FxKernel.ping(42), FxKernel.ping("s"), FxKernel.ping([1, :a])
p FxKernel.add(2, 40), FxKernel.add("a", "b"), FxKernel.add([1], [2])
c = FxKernel.make("c1")
p c.class, c.class.name, c.is_a?(FxKernel::Counter), FxKernel::Counter.superclass
p FxKernel.bump(c, 5), FxKernel.bump(c, 5)
puts c.inspect.sub(/0x\h+/, "0x")
p FxKernel.respond_to?(:ping), FxKernel.respond_to?(:nope)
p FxKernel.pair(3), [FxKernel, FxKernel::Counter].index(FxKernel::Counter)
begin
  FxKernel.boom("bad")
rescue ArgumentError => e
  p e.class, e.message
end
begin
  FxKernel.ping
rescue ArgumentError => e
  p e.message
end
# a Hash of the script for a Hash[Symbol, untyped] parameter, and a value
# that is no Hash
h = { name: "box", n: 3 }
p FxKernel.describe(h)
# CRuby raises NoMethodError in the body, the host build TypeError at the
# boundary
begin
  FxKernel.describe([1])
rescue StandardError
  p :rejected
end
