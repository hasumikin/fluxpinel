# Kernel#putc, Signal.list, Signal.signame and Enumerator#first.

putc "A"
putc 66
putc 321
putc "\n"
p putc("ab")
p putc(10)
r = putc 67
puts
p r

p Signal.list["INT"]
p Signal.list.key?("EXIT")
p Signal.list["EXIT"]
p Signal.list.size > 20
p Signal.signame(2)
p Signal.signame(0)
p Signal.signame(999)

e = [1, 2, 3].each
p e.first
p e.first(2)
p e.first(0)
p e.first(5)
p [].each.first
p [].each.first(2)
p [4, 5].each_with_index.first
p [1, 2, 3].map.first
p "ab".each_char.first
p({ a: 1 }.each.first)
p({ a: 1, b: 2 }.each)
p({ a: 1, b: 2 }.each_pair.to_a)
p({ a: 1, b: 2 }.each_with_index.to_a)
p((1..3).each)
p((1..3).each.first(2))
p((1..3).each_with_index.to_a)
p({ a: 1 }.each.with_index(1).to_a)
p((1..).each.first(2))
p((1..Float::INFINITY).each)
p((1..Float::INFINITY).each.first(3))
p((7..Float::INFINITY).each_with_index.first(2))
p((1..2.5).each.to_a)
p((1...3.0).each.to_a)
p((1..2.5).to_a)
(1..Float::INFINITY).each { |i| break if i > 2; p i }
m = -9223372036854775807 - 1
p((m...m).to_a)
p((m..m).each.to_a)
begin
  e.first(-1)
rescue ArgumentError => x
  puts x.message
end
