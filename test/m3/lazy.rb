# Enumerator::Lazy (lib/fluxpinel/lazy.rb): the sources, the stages, the
# ends of a chain, and what a chain reads from its source.

# the class
l = (1..3).lazy
p l, l.class, Enumerator::Lazy.superclass, l.is_a?(Enumerator), l.is_a?(Enumerable)
p l.lazy.equal?(l), l.map { |x| x }.take(2).each_slice(1)
p [1, 2].each.lazy, { a: 1 }.lazy.to_a

# a source with no end
p (1..Float::INFINITY).lazy.select(&:even?).map { |x| x * x }.first(3)
p (1..).lazy.reject(&:odd?).first(2), ("a"..).lazy.map(&:upcase).first(3)
e = Enumerator.new { |y| i = 0; loop { y << (i += 1) } }
p e.lazy.filter_map { |x| x * 2 if x % 3 == 0 }.first(2)

class Count
  include Enumerable
  def each
    i = 0
    loop { yield(i += 1) }
  end
end
p Count.new.lazy.map { |x| x * 2 }.take_while { |x| x < 9 }.to_a, Count.new.lazy.include?(4)

# the stages
a = [3, 1, 4, 1, 5, 9, 2, 6]
p a.lazy.take(3).to_a, a.lazy.drop(5).to_a, a.lazy.drop_while { |x| x < 4 }.to_a
p a.lazy.uniq.to_a, [1, nil, 2].lazy.compact.to_a, a.lazy.grep(2..4).to_a, a.lazy.grep_v(2..4) { |x| -x }.to_a
p a.lazy.with_index.first(2), a.lazy.with_index(1).map { |x, i| x * i }.first(2), a.lazy.each_with_index.first(1)
p a.lazy.flat_map { |x| [x, x] }.first(3), a.lazy.flat_map { |x| x }.first(2), a.lazy.zip(1..3, [:a]).first(2)
p a.lazy.each_slice(3).to_a, a.lazy.each_cons(7).to_a, a.lazy.chunk_while { |x, y| x < y }.first(2)
p a.lazy.slice_when { |x, y| x > y }.to_a
p (1..3).lazy.collect { |x| x }.filter(&:odd?).collect_concat { |x| [x] }.force

# the ends of a chain
p l.first, l.first(2), l.first(0), [].lazy.first, l.to_a, l.force, l.eager.class
p l.find { |x| x > 1 }, l.find_index(3), l.any? { |x| x > 2 }, l.all?(Integer), l.none?(4)
p l.sum, l.map { |x| -x }.sort, l.count, l.min, l.reduce(:+), l.each_with_object([]) { |x, m| m << x }
p l.map { |x| x }.next, a.lazy.each { }.equal?(a), l.map { |x| x }.each { }

# the size
p l.size, l.map { |x| x }.size, l.select { |x| x }.size, l.take(2).size, l.take(5).size, l.drop(1).size
p (1..).lazy.size, (1..Float::INFINITY).lazy.map { |x| x }.size, [1, 2].lazy.size, e.lazy.size

# a chain reads no more values than it needs, and each end reads again
seen = []
c = (1..10).lazy.map { |x| seen << x; x }
p c.take(2).to_a, seen, c.first(1), seen, c.select(&:even?).first(1), seen
t = (1..10).lazy.take(2)
p t.to_a, t.to_a

# Lazy.new with a block
p Enumerator::Lazy.new([1, 2, 3]) { |y, v| y << v * 10 if v.odd? }.to_a
p Enumerator::Lazy.new(1..3) { |y, v| y.yield v; y.yield v }.map { |x| x + 1 }.first(3)

# the errors
begin
  l.map
rescue ArgumentError => err
  p err.message
end
begin
  l.take(-1)
rescue ArgumentError => err
  p err.message
end
