# The argument shapes of String, Symbol, Float, Exception, Method and the
# Kernel conversions that the wrappers of the runtime do not take.
s = [+"ab\ncd\nef", 1][0]
p s.each_line("b").to_a, s.each_line(chomp: true).to_a, s.each_line("d", chomp: true).to_a, s.each_line("c")
p s.each_line(nil).to_a, "".each_line(nil).to_a
p((+"hello").encode!("UTF-8"))
t = +"world"; t.encode!; p t
p "ab".upcase(:ascii), "ab".downcase(:ascii), "ab".capitalize(:ascii), "aB".swapcase(:ascii), "ab".upcase(:turkic), "AB".downcase(:fold), "ab".upcase(:turkic, :lithuanian), "ab".capitalize(:turkic)
["ab".upcase(:foo), "ab".upcase(:ascii, :turkic), "ab".upcase(:fold)].each { |x| p x } rescue p $!.message
begin; "ab".upcase(:foo); rescue ArgumentError => e; p e.message; end
begin; "ab".upcase(:ascii, :turkic); rescue ArgumentError => e; p e.message; end
begin; "ab".upcase(:fold); rescue ArgumentError => e; p e.message; end
begin; "ab".upcase(foo: 1); rescue ArgumentError => e; p e.message; end
p :ab.upcase(:ascii), :aB.swapcase(:ascii)
b = "\x01\x00\x01\x00\x00\x00\x02\x40\x49\x0f\xdb".b
p b.unpack1("C"), b.unpack1("L>", offset: 3), b.unpack1("C", offset: 2), b.unpack1("g", offset: 7).round(4), b.unpack("C*", offset: 9), "abc".unpack1("C", offset: 3)
begin; "abc".unpack1("C", offset: 9); rescue ArgumentError => e; p e.message; end
p "1 2 3".split, "  leading  trailing  ".split, "a\tb\nc".split, "".split, "abc".split(nil), "a,b,c".split(","), "a  b c".split(" ", 2), "a b c".split(nil, 2)
begin; "abc".scan(1); rescue TypeError => e; p e.message; end
begin; "abc".split(1); rescue TypeError => e; p e.message; end
begin; "abc".split(:a, 2); rescue TypeError => e; p e.message; end
p "a\xff".b.scan(/[^\xff]/n).map(&:encoding).uniq
p [65].pack("C", buffer: +"ab"), [1, 2].pack("C*", buffer: "x".b)
p :hello[1], :hello[1..2], :hello[/l+/], :hello["ell"], :hello[9]
p([1].dig(0, 1)) rescue p [$!.class, $!.message]
p({a: 1}.dig(:a, 1)) rescue p [$!.class, $!.message]
p({a: [1, {b: 2}]}.dig(:a, 1, :b))
p 1.5.coerce(2), 1.5.coerce("2"), 1.coerce(2.5)
begin; 1.5.coerce(nil); rescue TypeError => e; p e.message; end
begin; 1.5.coerce("x"); rescue ArgumentError => e; p e.message; end
class S; def to_str = "str!"; def to_s = "s!"; end
class T; def to_s = "t!"; end
class A; def to_a = [1]; end
p String(1), String(nil), String(:a), String(S.new), String(T.new), Array(nil), Array([1]), Array(1..2), Array(1), Array({a: 1}), Array(A.new), Hash(nil), Hash([]), Hash({a: 1})
p "a b ".split(" ", -1), " a".split(" ", 1), "a b".split(" ", 1), s.each_line(chomp: true), s.each_line("c", chomp: true)
begin; Hash(1); rescue TypeError => e; p e.message; end
p Float("1e5"), Float("x", exception: false), Float(nil, exception: false), Float(1), Float("1_000.5")
begin; Float(nil); rescue TypeError => e; p e.message; end
p nil =~ /a/, nil =~ 1
e = RuntimeError.new("y")
p e.exception("z").class, e.exception("z").message, e.exception("y").equal?(e), e.exception(e).equal?(e), e.exception.equal?(e), e.exception(nil).message, e.exception(:sym).message
class C
  def foo = "C"
  alias bar foo
  alias_method :baz, :bar
  define_method(:"odd name") { __method__ }
end
p C.new.method(:bar).original_name, C.new.method(:baz).original_name, C.new.method(:foo).original_name, C.new.method(:"odd name").original_name
p [1, 2].to_set.class, (1..3).to_set.size, [1, 2].each.each_with_index.to_a, 3.times.each_with_index.to_a
p [1, 2].repeated_combination(2).to_a, [1, 2].repeated_combination(2), [1, 2].repeated_permutation(2).to_a.size, [1, 2].repeated_combination(2) { |x| p x }
begin; (1..).to_a; rescue RangeError => e; p e.message; end
p (1..5).drop(2), (1..5).take(2)
