# The entries of the kernel (test/kernel/app.rb), called from Lupe. Only
# make test-host runs this file, and CRuby requires the kernel for its
# answer.

p LupeKernel.ping(42), LupeKernel.ping("s"), LupeKernel.ping([1, :a])
p LupeKernel.add(2, 40), LupeKernel.add("a", "b"), LupeKernel.add([1], [2])
c = LupeKernel.make("c1")
p c.class, c.class.name, c.is_a?(LupeKernel::Counter), LupeKernel::Counter.superclass
p LupeKernel.bump(c, 5), LupeKernel.bump(c, 5)
puts c.inspect.sub(/0x\h+/, "0x")
p LupeKernel.respond_to?(:ping), LupeKernel.respond_to?(:nope)
p LupeKernel.pair(3), [LupeKernel, LupeKernel::Counter].index(LupeKernel::Counter)
begin
  LupeKernel.boom("bad")
rescue ArgumentError => e
  p e.class, e.message
end
begin
  LupeKernel.ping
rescue ArgumentError => e
  p e.message
end
