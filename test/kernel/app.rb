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

    # optional and rest parameters, which the table of --ext-class holds
    # as a row for each count of arguments
    def step(k = 1, *more) = @n += k + more.size

    def scale(a, b = 2) = @n * a * b

    # a method that yields, and one with a block parameter after an
    # optional one: a script passes its block to them
    def each_n
      i = 0
      while i < @n
        yield i
        i += 1
      end
      self
    end

    def with_n(k = 1, &blk) = blk.call(@n + k)

    # a yield under an ensure clause, which a jump out of the block runs
    def guarded
      yield @n
    ensure
      @n += 100
    end
  end

  # a block that the kernel keeps, and calls after the call that passed it
  class Keeper
    def self.make = new

    def keep(&blk)
      @kept = blk
      self
    end

    def fire(n) = @kept.call(n)

    # the block runs in another thread of the kernel
    def in_thread(&blk) = Thread.new { blk.call(1) }.value
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

  # w has no type: spinel refuses the write of @sizes when a typed entry
  # and the `new` of the table of --ext-class, which has no type, both
  # make a Shape (matz/spinel#12056)
  #: (String, untyped) -> Shape
  def self.shape(name, w) = Shape.new(name, w)

  #: (Shape) -> Integer
  def self.grow(s) = s.grow

  # A String parameter: the wrapper of the entry takes both forms of a
  # String, and no other value
  #: (String) -> Integer
  def self.len(s) = s.bytesize

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

  # n threads of the kernel, each with a console session of its own, run
  # the same lines at the same time, as the sessions of a server do. The
  # answers come back in the order of the threads.
  #: (Integer, Array[String]) -> Array[String]
  def self.sessions(n, lines)
    threads = []
    i = 0
    while i < n
      threads << Thread.new(i) { |k| console(lines.map { |l| l.sub("K", k.to_s) }) }
      i += 1
    end
    threads.map(&:value)
  end
end

# A thread at the top level: the init of the kernel starts the scheduler,
# as a server does, before Fluxpinel starts (src/fiber.c, fx_sched_start)
FxKernel::STARTED = Thread.new { :started }.value
