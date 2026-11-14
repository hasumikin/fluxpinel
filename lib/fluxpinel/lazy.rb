# lazy.rb -- Enumerator::Lazy in Ruby.
#
# spinel compiles a chain of lazy calls to one loop and has no Lazy object
# in its runtime, so Fluxpinel has its own. The first read of the constant
# Enumerator::Lazy loads this file (src/global.c). `lazy` of an Array, a
# Hash, a Range, an Enumerator, a Struct and an Enumerable object makes
# `Enumerator::Lazy.new(self)` (src/builtin.c).
#
# A Lazy holds a source and the makers of its stages. A call of each makes
# the stages, calls each of the source, and passes a value through the
# stages to the block. A stage answers the value for the next stage, SKIP
# for no value, or a Many of several values. A stage that has all the
# values it wants sets the stop flag: each then leaves the source, after
# the block got the last value.
#
# The methods of Enumerable that this file does not write (sum, sort,
# count ...) make the Array of all the values first (fx_enum_core of
# src/iter.c).

class Enumerator
  class Lazy < Enumerator
    SKIP = Object.new
    Many = Struct.new(:vals)

    # What the block of Lazy.new(source) { |yielder, *values| } writes to.
    class Yielder
      attr_reader :vals

      def initialize
        @vals = []
      end

      def <<(v)
        @vals << v
        self
      end

      def yield(*vs)
        @vals << (vs.size > 1 ? vs : vs[0])
        nil
      end

      def to_proc
        method(:yield).to_proc
      end
    end

    # The slices of each_slice(n) as a source.
    class Slices
      def initialize(lazy, n)
        @lazy = lazy
        @n = n
      end

      def each
        buf = []
        @lazy.each do |v|
          buf << v
          if buf.size == @n
            yield buf
            buf = []
          end
        end
        yield buf unless buf.empty?
        self
      end
    end

    # The windows of each_cons(n) as a source.
    class Windows
      def initialize(lazy, n)
        @lazy = lazy
        @n = n
      end

      def each
        buf = []
        @lazy.each do |v|
          buf << v
          buf.shift if buf.size > @n
          yield buf.dup if buf.size == @n
        end
        self
      end
    end

    # The runs of chunk_while and slice_when as a source: a new run starts
    # where cut answers true for two neighbors.
    class Runs
      def initialize(lazy, cut)
        @lazy = lazy
        @cut = cut
      end

      def each
        run = nil
        prev = nil
        @lazy.each do |v|
          if run.nil?
            run = [v]
          elsif @cut.call(prev, v)
            yield run
            run = [v]
          else
            run << v
          end
          prev = v
        end
        yield run if run
        self
      end
    end

    def initialize(source = nil, size = nil, &blk)
      raise ArgumentError, "tried to call lazy new without a block" if !blk && !source.respond_to?(:each)
      @__fx_source = source
      @__fx_makers = []
      @__fx_parent = nil
      @__fx_label = nil
      @__fx_args = []
      @__fx_size = size
      @__fx_sized = !size.nil?
      return unless blk
      @__fx_makers = [lambda do |_stop|
        lambda do |v|
          y = Yielder.new
          v.is_a?(Array) ? blk.call(y, *v) : blk.call(y, v)
          y.vals.empty? ? SKIP : Many.new(y.vals)
        end
      end]
    end

    # A Lazy of this one and one more stage. size is :same for a stage that
    # keeps the count of the values, nil for one that does not, or a Proc
    # that makes the count from the count before.
    def __fx_stage(label, args, size, &maker)
      nxt = Lazy.allocate
      nxt.__fx_set(@__fx_source, @__fx_makers + [maker], self, label, args, size)
      nxt
    end

    # A Lazy on a new source (the slices, the windows, the runs of this one).
    def __fx_resource(source, label, args)
      nxt = Lazy.allocate
      nxt.__fx_set(source, [], self, label, args, nil)
      nxt
    end

    def __fx_set(source, makers, parent, label, args, size)
      @__fx_source = source
      @__fx_makers = makers
      @__fx_parent = parent
      @__fx_label = label
      @__fx_args = args
      @__fx_size = size
      @__fx_sized = false
    end

    # With a block and no stage, each answers what each of the source
    # answers; with a stage it answers nil, as CRuby does.
    def each(&blk)
      return self unless blk
      return @__fx_source.each(&blk) if @__fx_makers.empty?
      stop = [false]
      stages = @__fx_makers.map { |m| m.call(stop) }
      return nil if stop[0]
      @__fx_source.each do |*xs|
        __fx_push(stages, 0, xs.size > 1 ? xs : xs[0], blk, stop)
        break if stop[0]
      end
      nil
    end

    def __fx_push(stages, i, v, blk, stop)
      n = stages.size
      while i < n
        v = stages[i].call(v)
        return if v.equal?(SKIP)
        if v.is_a?(Many)
          v.vals.each do |w|
            __fx_push(stages, i + 1, w, blk, stop)
            break if stop[0]
          end
          return
        end
        i += 1
      end
      blk.call(v)
    end

    # ---- the stages ----

    def map(&b)
      raise ArgumentError, "tried to call lazy map without a block" unless b
      __fx_stage(:map, [], :same) { |_| ->(v) { b.call(v) } }
    end
    alias collect map

    def select(&b)
      raise ArgumentError, "tried to call lazy select without a block" unless b
      __fx_stage(:select, [], nil) { |_| ->(v) { b.call(v) ? v : SKIP } }
    end
    alias filter select

    def reject(&b)
      raise ArgumentError, "tried to call lazy reject without a block" unless b
      __fx_stage(:reject, [], nil) { |_| ->(v) { b.call(v) ? SKIP : v } }
    end

    def filter_map(&b)
      raise ArgumentError, "tried to call lazy filter_map without a block" unless b
      __fx_stage(:filter_map, [], nil) { |_| ->(v) { b.call(v) || SKIP } }
    end

    def flat_map(&b)
      raise ArgumentError, "tried to call lazy flat_map without a block" unless b
      __fx_stage(:flat_map, [], nil) do |_|
        lambda do |v|
          r = b.call(v)
          if r.is_a?(Array) then Many.new(r)
          elsif r.is_a?(Lazy) then Many.new(r.force)
          elsif r.respond_to?(:to_ary)
            a = r.to_ary
            unless a.nil? || a.is_a?(Array)
              raise TypeError, "can't convert #{r.class} to Array (#{r.class}#to_ary gives #{a.class})"
            end
            a ? Many.new(a) : r
          elsif r.respond_to?(:force) && r.respond_to?(:each) then Many.new(r.force)
          else r
          end
        end
      end
    end
    alias collect_concat flat_map

    def take_while(&b)
      raise ArgumentError, "tried to call lazy take_while without a block" unless b
      __fx_stage(:take_while, [], nil) do |stop|
        lambda do |v|
          next v if b.call(v)
          stop[0] = true
          SKIP
        end
      end
    end

    def drop_while(&b)
      raise ArgumentError, "tried to call lazy drop_while without a block" unless b
      __fx_stage(:drop_while, [], nil) do |_|
        dropping = true
        ->(v) { dropping &&= b.call(v); dropping ? SKIP : v }
      end
    end

    def take(n)
      raise ArgumentError, "attempt to take negative size" if n < 0
      __fx_stage(:take, [n], ->(s) { s < n ? s : n }) do |stop|
        left = n
        stop[0] = true if left == 0
        lambda do |v|
          left -= 1
          stop[0] = true if left <= 0
          v
        end
      end
    end

    def drop(n)
      raise ArgumentError, "attempt to drop negative size" if n < 0
      __fx_stage(:drop, [n], ->(s) { s > n ? s - n : 0 }) do |_|
        left = n
        ->(v) { left > 0 ? (left -= 1; SKIP) : v }
      end
    end

    def with_index(offset = 0, &b)
      offset ||= 0
      __fx_stage(:with_index, offset == 0 ? [] : [offset], :same) do |_|
        i = offset - 1
        if b then ->(v) { b.call(v, i += 1); v }
        else ->(v) { [v, i += 1] }
        end
      end
    end

    def each_with_index(&b)
      __fx_stage(:each_with_index, [], :same) do |_|
        i = -1
        if b then ->(v) { b.call(v, i += 1); v }
        else ->(v) { [v, i += 1] }
        end
      end
    end

    def compact
      __fx_stage(:compact, [], nil) { |_| ->(v) { v.nil? ? SKIP : v } }
    end

    def uniq(&b)
      __fx_stage(:uniq, [], nil) do |_|
        seen = {}
        lambda do |v|
          k = b ? b.call(v) : v
          next SKIP if seen.key?(k)
          seen[k] = true
          v
        end
      end
    end

    def grep(pat, &b)
      __fx_stage(:grep, [pat], nil) { |_| ->(v) { pat === v ? (b ? b.call(v) : v) : SKIP } }
    end

    def grep_v(pat, &b)
      __fx_stage(:grep_v, [pat], nil) { |_| ->(v) { pat === v ? SKIP : (b ? b.call(v) : v) } }
    end

    def zip(*others, &b)
      return to_a.zip(*others, &b) if b
      __fx_stage(:zip, others, :same) do |_|
        i = -1
        ext = others.map { |o| o.is_a?(Array) ? o : o.to_enum }
        lambda do |v|
          i += 1
          rest = ext.map do |o|
            if o.is_a?(Array) then o[i]
            else
              begin
                o.next
              rescue StopIteration
                nil
              end
            end
          end
          [v, *rest]
        end
      end
    end

    def each_slice(n)
      raise ArgumentError, "invalid slice size" if n <= 0
      __fx_resource(Slices.new(self, n), :each_slice, [n])
    end

    def each_cons(n)
      raise ArgumentError, "invalid size" if n <= 0
      __fx_resource(Windows.new(self, n), :each_cons, [n])
    end

    def chunk_while(&b)
      __fx_resource(Runs.new(self, ->(x, y) { !b.call(x, y) }), :chunk_while, [])
    end

    def slice_when(&b)
      __fx_resource(Runs.new(self, b), :slice_when, [])
    end

    # ---- the ends of a chain ----

    def first(*args)
      raise ArgumentError, "wrong number of arguments (given #{args.size}, expected 0..1)" if args.size > 1
      if args.empty?
        each { |v| return v }
        return nil
      end
      n = args[0]
      raise ArgumentError, "attempt to take negative size" if n < 0
      out = []
      return out if n == 0
      each do |v|
        out << v
        break if out.size >= n
      end
      out
    end

    def to_a
      out = []
      each { |v| out << v }
      out
    end
    alias force to_a
    alias entries to_a

    def include?(x)
      each { |v| return true if v == x }
      false
    end
    alias member? include?

    def find(ifnone = nil)
      each { |v| return v if yield(v) }
      ifnone&.call
    end
    alias detect find

    def find_index(*args)
      i = 0
      each do |v|
        return i if args.empty? ? yield(v) : v == args[0]
        i += 1
      end
      nil
    end

    def any?(*pat)
      each { |v| return true if pat.empty? ? (block_given? ? yield(v) : v) : pat[0] === v }
      false
    end

    def all?(*pat)
      each { |v| return false unless pat.empty? ? (block_given? ? yield(v) : v) : pat[0] === v }
      true
    end

    def none?(*pat)
      each { |v| return false if pat.empty? ? (block_given? ? yield(v) : v) : pat[0] === v }
      true
    end

    def lazy
      self
    end

    def eager
      lz = self
      Enumerator.new { |y| lz.each { |v| y << v } }
    end

    def size
      return @__fx_size if @__fx_sized
      return @__fx_makers.empty? ? __fx_source_size : nil unless @__fx_parent
      return nil if @__fx_size.nil?
      s = @__fx_parent.size
      return nil if s.nil?
      @__fx_size == :same ? s : @__fx_size.call(s)
    end

    # The size of the source: Infinity for a Range of Integers with no end,
    # nil for a source that does not know its size.
    def __fx_source_size
      s = @__fx_source
      if s.is_a?(Range)
        return nil unless s.begin.is_a?(Integer)
        e = s.end
        return Float::INFINITY if e.nil? || (e.is_a?(Float) && e.infinite? == 1)
      end
      return nil unless s.respond_to?(:size)
      begin
        s.size
      rescue NoMethodError
        nil
      end
    end

    def __fx_external
      lz = self
      @__fx_external ||= Enumerator.new { |y| lz.each { |v| y << v } }
    end

    def next
      __fx_external.next
    end

    def peek
      __fx_external.peek
    end

    def rewind
      @__fx_external = nil
      self
    end

    def inspect
      return "#<Enumerator::Lazy: #{@__fx_source.inspect}>" unless @__fx_parent
      args = @__fx_args.empty? ? "" : "(#{@__fx_args.map(&:inspect).join(', ')})"
      "#<Enumerator::Lazy: #{@__fx_parent.inspect}:#{@__fx_label}#{args}>"
    end
    alias to_s inspect
  end
end
