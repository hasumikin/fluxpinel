# Ranges of Floats, Strings and Symbols, a Float end on an Integer
# Range, and open sides: the sides, ===, include?, cover?, ==, the walk
# of a String Range, and the errors of a Float Range that cannot walk.

f = 1.5..3.0
p f, f.begin, f.end, f.first, f.last, f.exclude_end?
p f === 2, f === 3.0, (1.5...3.0) === 3.0, f.include?(1.4), f.cover?(2.25), f === "x"
p f == (1.5..3.0), f == (1.5...3.0), f.eql?(1.5..3.0)
p (1..2.5), (1..2.5).end, (1..2.5).include?(2.5), (1...2.5).to_a, (1..2.5).to_a
p (0.5..), (..0.5), (0.5..).begin, (..0.5).end, (0.5..) === 100, (..0.5) === 1
p (1..3.0).sum, (1.0..2).end, (1.0..2) === 2

begin
  f.each { |x| p x }
rescue TypeError => e
  p e.message
end
begin
  f.to_a
rescue TypeError => e
  p e.message
end

s = "a".."e"
p s, s.begin, s.end, s.first, s.last, s.to_a, s.count, s.size
p s === "c", s === "cc", s.include?("cc"), s.cover?("cc"), s === :c, s === 3
p ("aa".."ad").to_a, ("aa".."ad").include?("ab"), ("aa".."ad").include?("b")
p s.map(&:upcase), s.select { |c| c > "b" }, s.each_slice(2).to_a
r = []
("x"..."z").each { |c| r << c }
p r, ("a".."c") == ("a".."c"), ("a".."c") == ("a"..."c")
p ("a"..), (.."m"), ("a"..) === "zzz", (.."m") === "b"

y = :a..:d
p y, y.begin, y.to_a, y === :b, y === "b", y.include?(:c)

def grade(score)
  case score
  when 90.0.. then "A"
  when 80.0...90.0 then "B"
  else "C"
  end
end
p [95.5, 85, 70.25].map { |v| grade(v) }

def bucket(w)
  case w
  when "a".."m" then :first_half
  when "n".."z" then :second_half
  end
end
p %w[apple nut zebra].map { |w| bucket(w) }
p (nil..nil), (nil..nil) === 5, (nil..nil) === "x"
p (1..5) == (1.0..5.0), (1.0..5.0) == (1..5), (1...5) == (1.0..5.0)
begin
  ("a"..).include?("b")
rescue TypeError => e
  p e.message
end

# step and % with a block and as an Enumerator
p (1..10).step(3).to_a, (1...10).step(3).to_a, (10..1).step(-3).to_a, (1..10).step(-1).to_a
p (1.0..2.0).step(0.5).to_a, (1..3).step(0.5).to_a, (0.0..1.0).step(0.1).to_a.size, (1.0..6.4).step(1.8).to_a
acc = []
(1..10).step(4) { |i| acc << i }
(2.0..1.0).step(-0.5) { |x| acc << x }
(1..).step(5) { |i| acc << i; break if i > 10 }
p acc
p ("a".."e").step(2).to_a, (("a".."e") % 2).to_a, ("a".."e").step(2), ("a"..).step(3).first(3)
s = []
("a".."g").step(3) { |c| s << c }
p s, (1..).step(3).first(3), (1.5..).step(0.5).first(3)
[0, "x", nil].each do |n|
  begin
    (1.0..2.0).step(n) { }
  rescue ArgumentError, TypeError => e
    p [e.class, e.message]
  end
end

# min, max and minmax read the sides; with an argument or a block they walk
p (1.0..2.5).min, (1.0..2.5).max, (1.0..2.5).minmax, (2.0..1.0).min, (1..5.5).max, (1..5.5).last
p (1..).min, (..5).max, (...5).max, (1..5).minmax, ("a".."e").minmax
p (1..5).min(2), (1..5).max(2), (1..5).max { |a, b| (a % 3) <=> (b % 3) }, (1..5).minmax { |a, b| b <=> a }
p (..4).max(2), (1..10).last(3), (1..10).count(&:even?), (1..10).count(4)
[-> { (1.0...2.0).max }, -> { (1.0..).max }, -> { (..1.0).min }, -> { (1.0..2.0).min(1) },
 -> { (1..5).first(-1) }, -> { (1..5).max(-1) }].each do |f|
  begin
    p f.call
  rescue ArgumentError, TypeError, RangeError => e
    p [e.class, e.message]
  end
end
p (1.0..).count, (..1.0).count

# an endless String Range walks one element at a time
w = []
("a"..).each { |c| break if c == "e"; w << c }
p w, ("x"..).first(4), ("a"..).each_slice(2).first(2), ("a"..).find_index("c")

# overlap? and cover? with a Range
p (1.0..2.0).overlap?(1.5..3.0), (1.0...2.0).overlap?(2..3), (1.0..).overlap?(..0), ("a".."e").overlap?("e".."z")
p ("a".."e").overlap?(100..200), (.."e").overlap?(..5), ("c".."a").overlap?("a".."z")
p (1.0..5.0).cover?(2..3), (1.0..5.0).cover?(0..3), (1..3).cover?(1.5..2.5), (1..3).cover?(1...4), ("a".."z").cover?("b".."d")
begin
  (1..2).overlap?(3)
rescue TypeError => e
  p e.message
end
