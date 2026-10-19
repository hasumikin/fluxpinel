# The builtin methods that take a block, on Array, Range and Integer.

a = [3, 1, 4, 1, 5, 9, 2, 6]
p a.map { |x| x * 2 }
p a.select(&:even?)
p a.reject { |x| x > 3 }
p a.filter_map { |x| x * 10 if x.odd? }
p a.flat_map { |x| [x, x] }.size
p a.find { |x| x > 4 }
p a.detect { |x| x > 100 }
p a.find_index { |x| x == 4 }
p a.count(&:odd?)
p a.any? { |x| x > 8 }
p a.all? { |x| x > 0 }
p a.none? { |x| x > 9 }
p a.one? { |x| x == 9 }
p a.inject { |s, x| s + x }
p a.reduce(100) { |s, x| s - x }
p a.inject(:+)
p a.reduce(1, :*)
p a.sum { |x| x * x }
p [0.1, 0.2, 0.3].sum { |x| x }
p a.min_by { |x| -x }
p a.max_by { |x| x % 5 }
p a.sort_by { |x| -x }
p a.sort { |x, y| y <=> x }
p a.min { |x, y| x <=> y }
p a.max { |x, y| x <=> y }
p a.partition(&:even?)
p a.take_while { |x| x < 5 }
p a.drop_while { |x| x < 5 }
a.each_slice(3) { |s| p s }
a.each_cons(7) { |s| p s }
p a.each_with_object([]) { |x, acc| acc << x if x > 4 }
a.reverse_each { |x| print x }
puts

b = [1, 2, 3, 4, 5]
b.map! { |x| x * x }
p b
p b.select! { |x| x > 1 }
p b.select! { |x| x > 1 }
b.delete_if { |x| x > 10 }
p b
p b.reject! { |x| x > 100 }

p (1..5).map { |x| x * x }
p (1...5).select(&:even?)
p (1..10).inject(:+)
p (1..3).to_a
p (1..10).sum
p (1..10).include?(5)
p (1...10).size
(1..3).each { |i| print i }
puts
(1..).each { |i| break if i > 3; print i }
puts

5.times { |i| print i }
puts
1.upto(3) { |i| print i }
3.downto(1) { |i| print i }
1.step(10, 3) { |i| print i }
puts

p Array.new(3) { |i| i * i }
p [1, 2].tap { |x| p x.size }
p 5.then { |x| x + 1 }
"héllo".each_char { |c| print c, "." }
puts

words = %w[pear fig apple]
p words.sort_by(&:size)
p words.map(&:length).sum
p words.min_by(&:size)
