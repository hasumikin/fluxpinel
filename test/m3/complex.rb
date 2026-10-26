# Kernel#Complex, the imaginary literal, the class methods of Complex and
# the methods of a Complex. Rational parts are left out: spinel keeps a
# Rational part as its Float.

c = Complex(1, 2)
p c, 2i, 1.5i, 3 + 4i, Complex(1.5, 2), Complex(3), Complex("1+2i"), Complex("3"), Complex(c), Complex(c, 1), Complex(1, c)
p c.real, c.imaginary, c.imag, c.abs, c.abs2, c.arg, c.conjugate, c.rectangular, c.polar, c.class, c.real?

# arithmetic with Integers and Floats on either side
p c + 1, c - c, c * c, c ** 2, -c, 1 + c, 2 * c, 1.0 - c, c.fdiv(2), [c, 2i].sum

# equality with any number, and no order
p c == Complex(1, 2), c == 1, Complex(1, 0) == 1, c != 1, c == "x", c.eql?(Complex(1, 2)), c.hash == Complex(1, 2).hash
p c.to_s, c.inspect, Complex(1, -2), Complex(-0.0, 1), c.finite?, c.zero?, Complex(0, 0).zero?, c.frozen?
p Complex(2, 0) <=> 3, 2 <=> Complex(1, 0), c <=> 1, 1 <=> c, 2 ** Complex(0, 1), 1.5 * c, 3 - c
p(case Complex(3, 0) when 3 then :integer else :other end)

# == hands any other value to its own ==
class Box
  def initialize(v) = (@v = v)
  def ==(other) = other == @v
end
p c == Box.new(c), c == Object.new

p Complex.rect(1, 2), Complex.rectangular(1.5), Complex.polar(2, 0), Complex.polar(1), Complex(3, 0).to_i
p 3.to_c, 1.5.to_c, nil.to_c, "1+2i".to_c, 2.i
p Complex(nil, exception: false), Complex("x", exception: false), Complex(:a, exception: false)
p((Complex(1, exception: nil) rescue $!), (Rational(1, exception: 1) rescue $!), (Integer("1", exception: nil) rescue $!))

[-> { Complex(nil) }, -> { Complex(:a) }, -> { Complex(1, :a) }, -> { Complex(true) }, -> { Complex("x") },
 -> { Complex.rect(:a) }, -> { c.to_i }, -> { c < c }, -> { c.between?(1, 2) }, -> { c.quo(:s) }].each do |f|
  f.call
rescue => e
  p e.class, e.message
end
