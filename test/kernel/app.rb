# The kernel of the host build of Lupe (make test-host): a program that
# spinel emits as a library. Lupe links it and runs test/m2 and test/m3 on
# its runtime state.

module LupeKernel
  def self.ping(x) = x
end
