# The kernel of the host build of Lupe (make test-host): a program that
# spinel emits as a library. Lupe links it, runs test/m2 and test/m3 on its
# runtime state, and calls its entries from test/host. CRuby requires this
# file for the answer of test/host.

module LupeKernel
  class Counter
    attr_reader :name, :n

    def initialize(name)
      @name = name
      @n = 0
    end

    def bump(k) = @n += k
  end

  def self.ping(x) = x
  def self.add(a, b) = a + b
  def self.make(name) = Counter.new(name)
  def self.bump(c, k) = c.bump(k)
  def self.boom(msg) = raise(ArgumentError, msg)
  def self.pair(a) = [a, a]
end
