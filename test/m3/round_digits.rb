# round, floor, ceil and truncate of Integer and Float with a digit count,
# and round with half:

p 3.14159.round(2), 3.14159.round(0), 1234.5678.round(-2), 2.5.round(half: :even)
p 2.5.round(half: :up), 2.5.round(half: :down), 2.675.round(2), -1.5.round
p 3.7.floor(1), 3.14159.floor(3), 3.14159.ceil(2), -3.14159.truncate(2), 1234.5.floor(-2)
p 15.round(-1), 25.round(-1, half: :even), 1234.floor(-2), 1234.ceil(-2), 1234.truncate(-3)
p 12.round(2), 12.5.round(1.9), 10.0.round(1).class, 10.5.round(0).class
p 64.781995.round(5), -64.781995.round(5), 1.005.round(2), 0.0000001.round(3)
p 1000000000000000005.round(-1), 9007199254740993.round(-3), 25.round(-1, half: :down)
p 2.5.round, -2.5.round, 2.7.truncate, -2.7.to_i, 2.7.to_int, 1e20.to_i
[Float::NAN, Float::INFINITY].each do |x|
  begin
    p x.round
  rescue FloatDomainError => e
    p e.message
  end
end
begin
  2.5.round(half: :bogus)
rescue ArgumentError => e
  p e.message
end
begin
  1.5.round(1, 2)
rescue ArgumentError => e
  p e.message
end
