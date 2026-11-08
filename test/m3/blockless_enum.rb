# The blockless forms that answer an Enumerator: each_slice, each_cons,
# cycle, each_index, combination, reverse_each of a Range, sort_by, and
# to_enum over a method of the program or of the runtime.
e = [1, 2, 3].to_enum(:each_slice, 2)
p e, e.to_a, e.size
p 3.to_enum(:times).to_a, 3.to_enum(:times).size
class Foo
  def each
    yield 1
    yield 2
  end
  def pairs(n)
    n.times { |i| yield i, i * 2 }
  end
end
f = Foo.new.to_enum(:each)
p f.to_a, f.size, f.next, f.inspect.sub(/0x\h+/, "X")
p Foo.new.enum_for.to_a, Foo.new.to_enum.inspect.sub(/0x\h+/, "X")
p Foo.new.to_enum(:pairs, 2).to_a, Foo.new.to_enum(:pairs, 2).inspect.sub(/0x\h+/, "X")

p [].each_index, [1, 2].each_index.to_a, %w[a b].each_index.map { |i| i * 2 }
p (1..3).reverse_each, (1..3).reverse_each.to_a, {a: 1, b: 2}.reverse_each.to_a
p [1, 2, 3].combination(2), [1, 2, 3].combination(2).to_a, [1, 2, 3].combination(2).size
p [1, 2, 3].combination(2).select { |a, b| a + b > 3 }
p [1, 2].cycle, [1, 2].cycle(2), [1, 2].cycle(2).to_a, [1, 2].cycle(nil).first(3), [1, 2].cycle.size, [1, 2].cycle.take(5)
p [3, 1, 2].sort_by, [3, 1, 2].sort_by.with_index { |x, i| -i }, [3, 1, 2].sort_by.each { |x| x }

p (1..4).each_slice(2).to_a, {a: 1, b: 2}.each_slice(1).to_a, [1, 2, 3].each_slice(2), [1, 2, 3].each_cons(2).size
p [1, 2, 3].each.each_slice(2).to_a, (1..).each_slice(2).first(2), (1..).each_cons(2).first(2)
p [1, 2, 3, 4].each_slice(2).map { |a, b| a + b }, [1, 2, 3].each_cons(2).with_index.to_a
p [1, 2, 3].each_slice(2).include?([3]), [1, 2, 3].each_slice(2).include?([2]), (1..).each.include?(3)
begin
  [1, 2].each_slice(0)
rescue ArgumentError => e
  p e.message
end
