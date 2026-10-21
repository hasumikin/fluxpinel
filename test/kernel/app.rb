# The kernel of the host build of Fluxpinel (make test-host): a program that
# spinel emits as a library. Fluxpinel links it, runs test/m2 and test/m3 on its
# runtime state, and calls its entries from test/host. CRuby requires
# kernel/fluxpinel_cruby.rb and this file for the answer of test/host.

require_relative "../../kernel/fluxpinel"

module FxKernel
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

  # AOT code that calls Fluxpinel: each file names a plugin, which answers its
  # greeting through a method of Fluxpinel
  def self.greet_all(paths, who)
    out = []
    paths.each do |path|
      plugin = Fluxpinel.load(path)
      out << Fluxpinel.call(plugin, "greet", [who])
    end
    out
  end

  def self.script(src) = Fluxpinel.eval(src)

  def self.rescue_script(src)
    Fluxpinel.eval(src)
  rescue ArgumentError => e
    "rescued in AOT: #{e.message}"
  end
end
