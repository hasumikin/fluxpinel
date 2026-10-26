# Kernel#Rational, the Rational literal and the methods of a Rational

r = Rational(3, 4)
p r, 3r, 1.5r, -2r, Rational(6, 4), Rational(4, 2), Rational("1/3"), Rational("0.75"), Rational(2.5), Rational(5)
p Rational(r), Rational(r).equal?(r), Rational(1.5, 2), Rational("1", "3")
p r.numerator, r.denominator, r.to_f, r.to_s, r.class, r.is_a?(Numeric), r.finite?, r.zero?

# arithmetic with Integers and Floats on either side
p r + 1, r - r, r * 2, r / 3, r + 0.5, 1 + r, 2 * r, 1.0 + r, r ** 2, -r, r.abs
p r.divmod(Rational(1, 3)), r % Rational(1, 3), r.fdiv(2), 7r / 2, [r, 1r, 2r].sum, [r, 1r].max

# comparison and rounding
p r < 1, r == Rational(3, 4), r == 0.75, r <=> 1, 1 <=> r, r.eql?(Rational(3, 4)), Integer === r, Rational === r
p r.round, r.floor, r.ceil, r.truncate, r.to_i, r.round(1), Rational(1, 3).round(2), Rational(5, 2).round(half: :even)
p r.hash == Rational(3, 4).hash, { r => 1 }[Rational(6, 8)]

# Integer, Float and nil give a Rational
p 7.div(2r), 7.modulo(2r), 7.divmod(2r), 1r.frozen?
p 1.quo(3), 3.to_r, 0.5.to_r, 0.1.rationalize(Rational(1, 100)), 0.333.rationalize, nil.to_r, 3.rationalize

# a Range of Integers or Floats covers a Rational by its value
p (1..3).include?(Rational(5, 2)), (1..3) === Rational(7, 2), (1.0..3.0).cover?(r), (1...3).member?(3r)
case Rational(3, 2)
when 1..2 then p :low
else p :out
end

p Rational(nil, exception: false), Rational("x", exception: false), Rational(:a, exception: false)
[-> { Rational(1, 0) }, -> { Rational(nil) }, -> { Rational(:a) }, -> { r.round(1.5) }].each do |f|
  f.call
rescue => e
  p e.class, e.message
end
