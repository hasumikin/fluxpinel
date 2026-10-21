# The kernel of the host build of Lupe (make test-host): a program that
# spinel emits as a library. Lupe links it, runs test/m2 and test/m3 on its
# runtime state, and calls its entries from test/host. CRuby requires
# kernel/lupe_cruby.rb and this file for the answer of test/host.

require_relative "../../kernel/lupe"

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

  # AOT code that calls Lupe: each file names a plugin, which answers its
  # greeting through a method of Lupe
  def self.greet_all(paths, who)
    out = []
    paths.each do |path|
      plugin = Lupe.load(path)
      out << Lupe.call(plugin, "greet", [who])
    end
    out
  end

  def self.script(src) = Lupe.eval(src)

  def self.rescue_script(src)
    Lupe.eval(src)
  rescue ArgumentError => e
    "rescued in AOT: #{e.message}"
  end
end
