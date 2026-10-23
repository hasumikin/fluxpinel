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

  # ivars of several kinds, which a script reads with instance_variable_get
  class Shape
    def initialize(name, w)
      @name = name
      @w = w
      @ratio = w / 2.0
      @tags = ["a", name]
      @sizes = { "w" => w }
      @owner = Counter.new(name)
      @grown = nil
    end

    # @late is set here first, so instance_variables lists it only after
    def grow
      @late = :late
      @grown = @w + 1
    end
  end

  #: (String, Integer) -> Shape
  def self.shape(name, w) = Shape.new(name, w)

  #: (Shape) -> Integer
  def self.grow(s) = s.grow

  # A Hash whose values are of any kind: a script passes a Hash of its own,
  # and the entry gets a copy of the kind of the parameter
  #: (Hash[Symbol, untyped]) -> String
  def self.describe(attrs)
    attrs.keys.map(&:to_s).sort.join(",") + " " + attrs[:name].to_s
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

  # AOT code that drives a console session, as a socket server would
  #: (Array[String]) -> String
  def self.console(lines)
    session = Fluxpinel.session
    out = "fx> "
    lines.each do |line|
      reply = Fluxpinel.feed(session, line)
      # nil: an exit closed the session
      break unless reply
      out += reply
    end
    out
  end

  def self.rescue_script(src)
    Fluxpinel.eval(src)
  rescue ArgumentError => e
    "rescued in AOT: #{e.message}"
  end
end

# A thread at the top level: the init of the kernel starts the scheduler,
# as a server does, before Fluxpinel starts (src/fiber.c, fx_sched_start)
FxKernel::STARTED = Thread.new { :started }.value
