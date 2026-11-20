# Range.new(first, last, exclude_end = false): the Range of the literal,
# for each kind of end the literal takes.
p Range.new(1, 3), Range.new(1, 3, true), Range.new(1, 3, nil).exclude_end?, Range.new(1, 3, 1).exclude_end?
p Range.new(nil, nil), Range.new(1, nil), Range.new(nil, 3)
p Range.new("a", "c").to_a, Range.new(:a, :c).to_a
p Range.new(1.5, 2), Range.new(1, 2.5).to_a, Range.new(1.0, 2.0).include?(1.5)
p Range.new(1, 3) == (1..3), Range.new(1, 3, true) == (1...3), Range.new(1, 3).frozen?
p Range.new(1, 3).sum, Range.new(1, 3).map { |i| i * 2 }
case 5
when Range.new(1, 10) then p :in
end
begin
  Range.new(1, "a")
rescue ArgumentError => e
  p e.message
end
begin
  Range.new(1)
rescue ArgumentError => e
  p e.message
end
begin
  Range.new(1, 2, 3, 4)
rescue ArgumentError => e
  p e.message
end
