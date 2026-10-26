# The methods of an Integer that codegen emits inline, and the methods of a
# Bignum. Only literals make a Bignum here: the default build raises at an
# Integer overflow.

def t(label)
  r = yield
  puts "#{label}: #{r.inspect}"
rescue => e
  puts "#{label}: #{e.class} #{e.message}"
end

# a machine Integer
p 6.lcm(4), 7.ceildiv(2), 6.gcdlcm(4), 3.pow(4), 3.pow(4, 5), 7.remainder(-3), 5.coerce(2), 5.numerator, 5.denominator
p 6.allbits?(2), 6.anybits?(1), 6.nobits?(1), 5.between?(1, 9), 5.clamp(1, 3), 123.digits(16), 5.digits(10)

# a Bignum
b = 100000000000000000000
n = -100000000000000000050
p b.abs, n.magnitude, b.bit_length, n.bit_length, b.size, b.succ, b.pred, b.to_f, b.to_i.equal?(b), b.ord
p b.zero?, b.positive?, n.negative?, b.even?, n.odd?, b.integer?, b.finite?, b.infinite?, b.nonzero?, b.digits.size
p b.lcm(7), b.gcd(30), b.ceildiv(7), b.gcdlcm(6), b.pow(2), b.pow(3, 7), b.remainder(7), b.coerce(1), b.fdiv(3)
p b.to_s(16), n.to_s(2), b.digits(1000), b.allbits?(0), b.between?(1, b), n.clamp(1, 2), b.numerator, b.denominator

# rounding to negative digits: exact, and a half goes away from zero
p n.round(-2), n.floor(-2), n.ceil(-2), n.truncate(-2), b.round(-3), (b + 50).round(-2), b.floor(-25), b.round

# bits, the square root and Comparable
p b[66, 3], n[0, 70], b[64..66], b[60..], 0b1011[1.5, 2], Integer.sqrt(24), Integer.sqrt(b), Integer.sqrt(4.9)
p 5.clamp(1..3), 5.clamp(..3), 5.clamp(1...), 5.clamp(1.0..3.5), 1.5.clamp(2..3), 1.5.clamp(1, 2), 2.5.between?(1, 3)

# a Bignum as a Complex, and Float#fdiv and #remainder
p b.real?, b.real, b.imag, b.phase, n.arg, b.rect, n.polar, b.conj
p 7.5.fdiv(2), 7.5.fdiv(b), 7.5.fdiv(Rational(1, 2)), 7.5.fdiv(0), 7.5.remainder(2), -7.5.remainder(2.5), 7.5.remainder(b)

# step, upto, downto and then with no block: an Enumerator over the values
p 1.step(10, 3).to_a, 1.step(10, 3).size, 10.step(1, -4).to_a, 1.step(by: 2).first(3), 1.0.step(2.0, 0.5).to_a
p 1.step(Float::INFINITY, 2).first(2), 1.step(nil, 2).first(2), 1.step(1_000_000_000).first(2), 10.step(1, 3).to_a
p 1.upto(3), 3.downto(1).size, 1.upto(2.5).to_a, b.upto(b + 2).to_a, (b + 1).downto(b), 1.then, 1.then.to_a

t("chr") { b.chr }
t("radix") { b.to_s(1) }
t("digits of a negative") { n.digits }
t("digits base") { 5.digits(1) }
t("between? with a String") { b.between?("a", "b") }
t("shift width") { 5[1, b] }
t("sqrt of a negative") { Integer.sqrt(-n) }
t("sqrt of a String") { Integer.sqrt("4") }
t("clamp exclusive") { 5.clamp(1...3) }
t("clamp min over max") { 5.clamp(3, 1) }
t("clamp an Integer") { 5.clamp(3) }
t("fdiv of an Array") { 7.5.fdiv([1]) }
t("remainder of a Symbol") { 7.5.remainder(:a) }
t("remainder by 0") { 7.5.remainder(0) }
t("step 0") { 1.step(10, 0) }
t("step twice") { 1.step(10, 2, by: 3) }
t("to twice") { 1.step(10, to: 3) { } }
t("step nil") { 1.step(10, nil) { } }
t("step String") { 1.step(10, "x") { } }
t("beginless bits") { n[..2] }
