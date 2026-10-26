# The functions of Math, their conversion of the argument and their domain

p Math.sqrt(16), Math.sqrt(2.25), Math.cbrt(27), Math.sqrt(-0.0)
p Math.sin(0), Math.cos(0), Math.atan(1) * 4 == Math::PI, Math.tanh(0)
p Math.exp(0), Math.log(1), Math.log(8, 2), Math.log2(1024), Math.log10(1000)
p Math.log(10, 1).nan?, Math.log1p(0), Math.expm1(0)
p Math.atan2(1, 1) * 4 == Math::PI, Math.hypot(3, 4), Math.ldexp(0.5, 3)
p Math.frexp(8.0), Math.lgamma(1), Math.gamma(5), Math.erf(0), Math.erfc(0)
p Math.asinh(0), Math.acosh(1), Math.atanh(0), Math.asin(1) * 2 == Math::PI, Math.acos(1)
p Math.sqrt(1e20), Math.cbrt(-8), Math.cbrt(0.001)

[-> { Math.sqrt(-1) }, -> { Math.log(-1) }, -> { Math.acos(2) }, -> { Math.atanh(2) },
 -> { Math.sqrt(nil) }, -> { Math.sqrt("4") }].each do |l|
  begin
    p l.call
  rescue Math::DomainError, TypeError => e
    p [e.class, e.message]
  end
end
