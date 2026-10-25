# The builtin methods with a block that src/prelude.rb writes in Ruby.

a = [3, 1, 2]
idx = []
p a.each_index { |i| idx << i }
p idx
p(a.each_index { |i| break i * 10 if i == 1 })

p [1, 2, 3, 2].rindex { |x| x == 2 }
p [1, 2, 3].rindex { |x| x > 5 }
p [1, 2, 3, 2].rindex(2) { |x| x == 1 }

b = [5, 3, 9, 1]
p b.sort! { |x, y| y <=> x }
p b
c = %w[ccc a bb]
p c.sort_by! { |s| s.size }
p c

bytes = []
p "hé".each_byte { |x| bytes << x }
p bytes

def first_even(list)
  list.each_index { |i| return list[i] if list[i].even? }
  nil
end
p first_even([1, 3, 4, 6])

p [].respond_to?(:each_index)

# a top-level def of the program does not take a call of the prelude
def bytes(x) = raise("not reached")
p "ok".each_byte { }

pairs = [[1, :a], [0, :b], [2, :c], [0, :d], [2, :e]]
p pairs.minmax_by { |x| x[0] }
p pairs.min_by(2) { |x| x[0] }
scores = [["Bob", 92], ["Eve", 92], ["Ann", 88]]
p scores.max_by(2) { |x| x[1] }
p [3, 1, 2].max_by(2.7) { |x| x }
begin
  [1].min_by(-1) { }
rescue ArgumentError => e
  p e.message
end
begin
  1.5.step(3, "s") { }
rescue ArgumentError => e
  p e.message
end
p [1, 2, 4, 9, 10, 11, 12, 15].slice_when { |i, j| i + 1 != j }.to_a
p [1, 2, 4, 5].chunk_while { |x, y| y == x + 1 }.to_a
p [3, 1, 4, 1, 5].chunk { |x| x.odd? }.to_a
p [1, 2, nil, 3].chunk { |x| x.nil? ? :_separator : true }.to_a
p [1, 2, 3].chunk { |x| x == 2 ? :_alone : true }.to_a
p [].cycle { }
seen = []
p [1, 2].cycle(2) { |x| seen << x }
p seen
p [1, 2, 3].cycle { |x| break x * 7 if x == 3 }
p [1, 2, 3].fill { |i| i * i }
p [1, 2, 3].fill(1) { |i| -i }
p [1, 2, 3].fill(-2, 1) { |i| -i }
p [1, 2, 3].fill(1..) { |i| 0 }
p [1, 2, 3].fill(5, 1) { |i| 9 }
p [1, 2, 3].delete(5) { |x| "no #{x}" }
p [1, 2, 1].delete(1) { :no }
p [1, 2, 3, 4].uniq { |x| x % 2 }
p [1, 2, 3].find(-> { :none }) { |x| x > 5 }
p [1, 3, 5, 7].bsearch { |x| x >= 4 }
p [1, 3, 5, 7].bsearch { |x| 5 <=> x }
p (0..100).bsearch { |x| x * x >= 50 }
p [1, 2].zip([3, 4]) { |t| t }
sums = []
[1, 2, 3].zip([4, 5, 6]) { |x, y| sums << x + y }
p sums
p [1, 2].product([3]) { |t| t }
combos = []
p [1, 2, 3].combination(2) { |c| combos << c }
p combos
p({ a: 1, b: 2 }.fetch_values(:a, :c) { |k| k.to_s })
p({ a: 3, b: 1 }.min_by { |k, v| v })
p({ a: 3, b: 1 }.minmax_by { |k, v| v })

steps = []
1.step(10, 4) { |x| steps << x }
1.0.step(2.0, 0.25) { |x| steps << x }
10.step(1, -4) { |x| steps << x }
1.step(by: 2, to: 5) { |x| steps << x }
p steps
begin
  1.step(3, 0) { }
rescue ArgumentError => e
  p e.message
end

lines = []
"a\nb\r\nc".each_line { |l| lines << l }
"a\nb\r\nc".each_line(chomp: true) { |l| lines << l }
"a--b--".each_line("--") { |l| lines << l }
p lines
ups = []
"ab".upto("ad") { |s| ups << s }
"9".upto("11") { |s| ups << s }
p ups
