# map, select and their kin with no block answer an Enumerator that
# remembers the method; with_index, with_object and each collect as the
# method does.
a = [10, 20, 30]
p a.map
p a.map.size
p a.map.with_index { |x, i| x + i }
p a.map.with_index(1) { |x, i| [i, x] }
p a.map.each_with_index { |x, i| x * i }
p a.map.each { |x| x / 10 }
p a.collect.with_index { |x, i| i }
p a.select.with_index { |x, i| i.odd? }
p a.filter.with_index { |x, i| i.even? }
p a.reject.with_index { |x, i| i.even? }
p a.filter_map.with_index { |x, i| x if i > 0 }
p a.flat_map.with_index { |x, i| [x, i] }
p a.map.with_object([]) { |x, acc| acc << x }
p a.map.each_with_object(:memo) { |x, m| m }
p (1..3).map.with_index(10) { |x, i| x * i }
p({ a: 1, b: 2 }.map.with_index { |(k, v), i| [k, v, i] })
p a.map.with_index.to_a
p a.each.with_index(1).to_a
p a.map.with_index { |x, i| break i if i == 1 }
