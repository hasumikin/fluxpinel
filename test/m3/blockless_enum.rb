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

# to_enum keeps the keywords of the call as keywords: the method gets them
# by name, and inspect writes them without braces
class Kw
  def pairs(*a, **k)
    yield [a, k]
  end
  def inspect = "#<Kw>"
end
p Kw.new.to_enum(:pairs, 1, x: 2), Kw.new.to_enum(:pairs, x: 2), Kw.new.enum_for(:pairs, 1)
p Kw.new.to_enum(:pairs, 1, x: 2).to_a, Kw.new.to_enum(:pairs, x: 2).to_a, Kw.new.to_enum(:pairs, {x: 2}).to_a
h = {}
p Kw.new.to_enum(:pairs, **h), Kw.new.to_enum(:pairs, **h).to_a

# IO#each_line and its kin with no block: an Enumerator over the call,
# which reads the file when it runs
path = "/tmp/fluxpinel_blockless_io_#{$$}.txt"
File.write(path, "a\nb\n")
File.open(path) do |f|
  show = ->(e) { e.inspect.sub(path, "PATH") }
  p show[f.each_line], show[f.each_line("b")], show[f.each_line(chomp: true)], show[f.each_line("b", 1, chomp: true)]
  p show[f.each], show[f.each_byte], show[f.each_char]
  p f.each_line.to_a
  f.rewind
  p f.each_line(chomp: true).to_a, f.each_line.to_a
  f.rewind
  p f.each_line("b").to_a
  f.rewind
  p f.each_line(1).to_a
  f.rewind
  p f.each_byte.to_a
  f.rewind
  p f.each_char.first(2)
  f.rewind
  e = f.each_line
  p e.next, e.next
  begin
    e.next
  rescue StopIteration => x
    p x.class
  end
  f.rewind
  p f.each_line.with_index.to_a, f.each_line.size
  begin
    f.each_char(1)
  rescue ArgumentError => x
    puts x.message
  end
end
p IO.pipe[0].each_line.class, $stdin.each_line.class
File.unlink(path)
