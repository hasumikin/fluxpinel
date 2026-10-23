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
