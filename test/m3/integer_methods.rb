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
p n.round(-2), n.floor(-2), n.ceil(-2), n.truncate(-2), b.round(-3), (b + 50).round(-2), b.floor(-25)

t("chr") { b.chr }
t("radix") { b.to_s(1) }
t("digits of a negative") { n.digits }
t("digits base") { 5.digits(1) }
t("between? with a String") { b.between?("a", "b") }
