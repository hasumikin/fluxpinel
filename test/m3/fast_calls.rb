# The calls that fx_fast_call answers, and the chain after a reopen

x = 3
case x
when 1 then p :one
when 3 then p :three
end
case :b
when :a then p :a
when :b then p :b
end
p 7 / 2, -7 / 2, 7 % -3, 1.5 + 2.25, 1.5 < 2.5, 3 == 3, 3 != 4, 3 === 3
p 6 & 3, 6 | 3, 6 ^ 3, 1 << 4, 256 >> 2, 5[0], 5[1], 5[-1]
begin
  5 / 0
rescue ZeroDivisionError => e
  p e.message
end
y = 1
y += 2
y <<= 3
p y
a = [1, 2]
a[0] += 5
a[1] |= 8
a[3] = 4
p a, a[-1], a[9], a.size, a.length
h = { k: 1 }
h[:k] += 1
p h, h[:z]
d = Hash.new { |hh, k| hh[k] = k.to_s * 2 }
p d[:ab], d
b = [1].freeze
begin
  b << 2
rescue => e
  p e.class
end
begin
  b[0] = 2
rescue => e
  p e.class
end
c = []
c << 1 << 2
p c, :a == :a, :a == :b, :a === :a

class Integer
  def +(o) = 42
end
p 1 + 1
z = 1
z += 1
p z
