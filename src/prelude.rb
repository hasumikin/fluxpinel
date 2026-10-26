# prelude.rb -- builtin methods with a block, in Ruby (src/prelude.c).
#
# The wrappers of the spinel runtime drop a block, so Fluxpinel writes each
# builtin method that takes one. This file holds the ones that Ruby writes
# well. A call comes here only when no builtin of Fluxpinel takes the
# method with its block. The program does not see these definitions: a
# class here is not the builtin class, and does not reopen it.
#
# Each class names the builtin class of the receiver (Array, String, ...).
# Numeric serves Integer and Float, and IO serves a File too. The body can
# call any method that Fluxpinel knows. A call with no block never comes
# here.
#
# Write each call on the receiver as `self.name`. A call with no receiver
# also finds a top-level def of the program, which a builtin method of
# CRuby never calls. `__fx_gap__ "what"` stops at a case that this file
# does not write, as a gap of Fluxpinel.
#
# chunk_while, slice_when and chunk answer an Array, where CRuby answers
# an Enumerator of the same values.

class Array
  def each_index
    i = 0
    while i < self.size
      yield i
      i += 1
    end
    self
  end

  def rindex(*args)
    return self.rindex(*args) unless args.empty?
    i = self.size - 1
    while i >= 0
      return i if yield(self[i])
      i -= 1
      i = self.size - 1 if i >= self.size
    end
    nil
  end

  def sort!
    self.replace(self.sort { |a, b| yield a, b })
  end

  def sort_by!
    self.replace(self.sort_by { |e| yield e })
  end

  def find(ifnone = nil)
    self.each { |e| return e if yield(e) }
    ifnone&.call
  end

  def detect(ifnone = nil)
    self.each { |e| return e if yield(e) }
    ifnone&.call
  end

  def uniq
    seen = {}
    out = []
    self.each do |e|
      k = yield(e)
      next if seen.key?(k)
      seen[k] = true
      out << e
    end
    out
  end

  # nil when no element goes
  def uniq!
    out = self.uniq { |e| yield(e) }
    return nil if out.size == self.size
    self.replace(out)
  end

  # one walk, which compares each element once, as CRuby does
  def delete(obj)
    removed = nil
    hit = false
    i = 0
    while i < self.size
      if self[i] == obj
        removed = self[i]
        hit = true
        self.delete_at(i)
      else
        i += 1
      end
    end
    hit ? removed : yield(obj)
  end

  def fill(*args)
    raise ArgumentError, "wrong number of arguments (given #{args.size}, expected 0..2)" if args.size > 2
    n = self.size
    if args.empty?
      s = 0
      e = n
    elsif args[0].is_a?(Range)
      r = args[0]
      s = r.begin || 0
      s += n if s < 0
      raise RangeError, "#{r.inspect} out of range" if s < 0
      if r.end.nil?
        e = n
      else
        e = r.end
        e += n if e < 0
        e += 1 unless r.exclude_end?
      end
    else
      s = args[0] || 0
      s += n if s < 0
      s = 0 if s < 0
      e = args.size > 1 && !args[1].nil? ? s + args[1] : n
    end
    i = s
    while i < e
      self[i] = yield(i)
      i += 1
    end
    self
  end

  # min_by(n) and max_by(n): the first n by the key, ties in the order of
  # the walk. dir is 1 for min_by, -1 for max_by. CRuby picks them with an
  # unstable sort, so its order of ties can differ for a longer receiver.
  def __fx_nby(n, dir)
    if n.is_a?(Float)
      n = n < 0 ? n.ceil : n.floor
    elsif !n.is_a?(Integer)
      raise TypeError, "no implicit conversion of #{n.class} into Integer" unless n.respond_to?(:to_int)
      n = n.to_int
    end
    raise ArgumentError, "negative size (#{n})" if n < 0
    keyed = []
    self.each_with_index { |e, i| keyed << [yield(e), i, e] }
    keyed.sort! do |x, y|
      c = (x[0] <=> y[0]) * dir
      c == 0 ? x[1] <=> y[1] : c
    end
    keyed.first(n).map { |k| k[2] }
  end

  def min_by(n = nil)
    return self.__fx_nby(n, 1) { |e| yield e } unless n.nil?
    best = nil
    bv = nil
    first = true
    self.each do |e|
      v = yield(e)
      if first || (v <=> bv) < 0
        best = e
        bv = v
        first = false
      end
    end
    best
  end

  def max_by(n = nil)
    return self.__fx_nby(n, -1) { |e| yield e } unless n.nil?
    best = nil
    bv = nil
    first = true
    self.each do |e|
      v = yield(e)
      if first || (v <=> bv) > 0
        best = e
        bv = v
        first = false
      end
    end
    best
  end

  def minmax_by
    min = max = minv = maxv = nil
    first = true
    self.each do |e|
      v = yield(e)
      if first
        min = max = e
        minv = maxv = v
        first = false
      else
        if (v <=> minv) < 0
          min = e
          minv = v
        end
        if (v <=> maxv) > 0
          max = e
          maxv = v
        end
      end
    end
    [min, max]
  end

  def slice_when
    out = []
    cur = nil
    prev = nil
    self.each_with_index do |e, i|
      if i == 0
        cur = [e]
      elsif yield(prev, e)
        out << cur
        cur = [e]
      else
        cur << e
      end
      prev = e
    end
    out << cur if cur
    out
  end

  def chunk_while
    self.slice_when { |a, b| !yield(a, b) }
  end

  def chunk
    out = []
    key = nil
    cur = nil
    self.each do |e|
      k = yield(e)
      if k.nil? || k == :_separator
        out << [key, cur] if cur
        cur = nil
      elsif k == :_alone
        out << [key, cur] if cur
        out << [:_alone, [e]]
        cur = nil
      elsif k.is_a?(Symbol) && k.to_s.start_with?("_")
        raise RuntimeError, "symbols beginning with an underscore are reserved"
      elsif cur && k == key
        cur << e
      else
        out << [key, cur] if cur
        key = k
        cur = [e]
      end
    end
    out << [key, cur] if cur
    out
  end

  def cycle(n = nil)
    if n.nil?
      while true
        return nil if self.empty?
        self.each { |e| yield e }
      end
    end
    n = n.to_int
    while n > 0
      self.each { |e| yield e }
      n -= 1
    end
    nil
  end

  def zip(*others)
    self.zip(*others).each { |t| yield t }
    nil
  end

  def product(*others)
    self.product(*others).each { |t| yield t }
    self
  end

  def combination(k)
    n = self.size
    if k == 0
      yield []
    elsif k > 0 && k <= n
      idx = (0...k).to_a
      while true
        yield idx.map { |i| self[i] }
        j = k - 1
        j -= 1 while j >= 0 && idx[j] == j + n - k
        break if j < 0
        idx[j] += 1
        ((j + 1)...k).each { |m| idx[m] = idx[m - 1] + 1 }
      end
    end
    self
  end

  def bsearch_index
    lo = 0
    hi = self.size
    found = nil
    while lo < hi
      mid = lo + (hi - lo) / 2
      v = yield(self[mid])
      if v == true
        found = mid
        hi = mid
      elsif v == false || v.nil?
        lo = mid + 1
      elsif v.is_a?(Numeric)
        return mid if v == 0
        if v > 0
          lo = mid + 1
        else
          hi = mid
        end
      else
        raise TypeError, "wrong argument type #{v.class} (must be numeric, true, false or nil)"
      end
    end
    found
  end

  def bsearch
    i = self.bsearch_index { |e| yield e }
    i.nil? ? nil : self[i]
  end
end

class Hash
  def fetch_values(*keys)
    keys.map { |k| self.fetch(k) { |kk| yield kk } }
  end

  def min_by(n = nil)
    self.to_a.min_by(n) { |kv| yield kv }
  end

  def max_by(n = nil)
    self.to_a.max_by(n) { |kv| yield kv }
  end

  def minmax_by
    self.to_a.minmax_by { |kv| yield kv }
  end
end

class Range
  def bsearch
    lo = self.begin
    hi = self.end
    __fx_gap__ "Range#bsearch over a Range that is not of Integers" unless lo.is_a?(Integer) && hi.is_a?(Integer)
    hi += 1 unless self.exclude_end?
    found = nil
    while lo < hi
      mid = lo + (hi - lo) / 2
      v = yield(mid)
      if v == true
        found = mid
        hi = mid
      elsif v == false || v.nil?
        lo = mid + 1
      elsif v.is_a?(Numeric)
        return mid if v == 0
        if v > 0
          lo = mid + 1
        else
          hi = mid
        end
      else
        raise TypeError, "wrong argument type #{v.class} (must be numeric, true, false or nil)"
      end
    end
    found
  end
end

class Numeric
  # As CRuby's ruby_float_step: the count comes from the span, with a
  # margin for the rounding, and the last value is the limit.
  def step(*args, by: nil, to: nil)
    raise ArgumentError, "wrong number of arguments (given #{args.size}, expected 0..2)" if args.size > 2
    lim = args.size > 0 ? args[0] : to
    raise TypeError, "step must be numeric" if args.size > 1 && args[1].nil?
    st = args.size > 1 ? args[1] : (by.nil? ? 1 : by)
    raise ArgumentError, "comparison of #{st.class} with 0 failed" unless st.is_a?(Numeric)
    unless lim.nil? || lim.is_a?(Numeric)
      raise ArgumentError, "comparison of #{self.class} with #{lim.class} failed"
    end
    raise ArgumentError, "step can't be 0" if st == 0
    if self.is_a?(Float) || lim.is_a?(Float) || st.is_a?(Float)
      b = self.to_f
      u = st.to_f
      if lim.nil?
        i = 0
        while true
          yield b + i * u
          i += 1
        end
      end
      e = lim.to_f
      n = (e - b) / u
      return self if n < 0
      err = (b.abs + e.abs + (e - b).abs) / u.abs * 2.220446049250313e-16
      err = 0.5 if err > 0.5
      n = (n + err).floor
      i = 0
      while i <= n
        d = i * u + b
        d = e if u >= 0 ? e < d : d < e
        yield d
        i += 1
      end
    else
      i = self
      if lim.nil?
        while true
          yield i
          i += st
        end
      elsif st > 0
        while i <= lim
          yield i
          i += st
        end
      else
        while i >= lim
          yield i
          i += st
        end
      end
    end
    self
  end
end

class String
  def each_byte
    self.bytes.each { |b| yield b }
    self
  end

  def each_line(sep = "\n", chomp: false)
    if sep.nil?
      yield self unless self.empty?
      return self
    end
    __fx_gap__ "String#each_line in paragraph mode" if sep.empty?
    i = 0
    n = self.size
    while i < n
      j = self.index(sep, i)
      if j.nil?
        yield self[i, n - i]
        break
      end
      e = j + sep.size
      if chomp
        line = self[i, j - i]
        line = line[0, line.size - 1] if sep == "\n" && line.end_with?("\r")
        yield line
      else
        yield self[i, e - i]
      end
      i = e
    end
    self
  end

  def upto(last, exclusive = false)
    (exclusive ? (self...last) : (self..last)).each { |s| yield s }
    self
  end
end

class IO
  # gets takes the separator and the limit; a limit of 0 would read ""
  # for ever, so it raises as CRuby does
  def each_line(*args, chomp: false)
    raise ArgumentError, "invalid limit: 0 for each_line" if args.last == 0
    while (line = self.gets(*args, chomp: chomp))
      yield line
    end
    self
  end

  def each(*args, chomp: false)
    raise ArgumentError, "invalid limit: 0 for each_line" if args.last == 0
    while (line = self.gets(*args, chomp: chomp))
      yield line
    end
    self
  end

  def each_char
    while (c = self.getc)
      yield c
    end
    self
  end

  def each_byte
    while (b = self.getbyte)
      yield b
    end
    self
  end
end
