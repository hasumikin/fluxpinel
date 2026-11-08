# The argument shapes of Hash, Range, Enumerator and Array that the
# wrappers of the runtime do not take: replace, assoc, flatten, shift,
# default_proc=, find_index, sum(init), count, slice!, fill, permutation,
# << on a frozen Array, and a bad index of []=.
h = {a: 1, b: 2}
p h.replace({c: 3}), h, h.assoc(:c), h.assoc(:zz), h.rassoc(3), h.flatten, {a: [1, [2]]}.flatten(2), {a: [1]}.flatten(0)
p h.find_index([:c, 3]), h.find_index([:zz, 1]), h.shift, h.shift, h
h2 = Hash.new
h2.default_proc = proc { |hh, k| hh[k] = k * 2 }
p h2[3], h2.default_proc.class, h2, h.default_proc
h2.default_proc = nil
p h2[9], h2.default_proc
begin; h2.default_proc = 1; rescue TypeError => e; p e.message; end
begin; h.replace(1); rescue TypeError => e; p e.message; end
box = [{a: 1, b: 2, c: nil}, 0][0]
p box.shift, box.replace(z: 3, y: 4), box
begin; (..3).to_a; rescue TypeError => e; p e.message; end
p ("a".."c").sum(""), (1..3).sum(10), 3.times.count, 3.times.count(1), [1, 2].each.count, (1..3).each_slice(2).count
p [1, 2, 3].permutation(2).to_a, [1, 2, 3].permutation.to_a.size, [1, 2, 3].permutation(0).to_a, [1, 2, 3].permutation(2), [1, 2].permutation(1) { |x| p x }
a = [1, 2, 3, 4]
p a.slice!(1), a, a.slice!(1, 2), a, [1, 2, 3].slice!(0..1), [1].slice!(9), [1, 2, 3].slice!(-1)
p [1, 2, 3].slice!(1..), [1, 2, 3].slice!(5..), [1, 2, 3].slice!(-5, 2), [1, 2, 3].slice!(1, -1), [1, 2, 3].slice!(3, 1), [1, 2, 3].slice!(3)
begin; [1].freeze.slice!(0); rescue FrozenError => e; p e.message; end
a = [1, 2, 3, 4]
p a.fill(0, 1, 2), a.fill(9, 2..), [1, 2].fill(0, 1.5), [1, 2, 3].fill(7, -1), [1, 2, 3, 4].fill(0, 1.0..2.0), [1, 2, 3, 4].fill(0, 1...3)
p [1, 2, 3, 4].fill(0, 2..5), [1, 2, 3, 4].fill(0, 1..-2), [1, 2, 3, 4].fill(0, -3...-1), [1, 2, 3, 4].fill(0, ..1), [1, 2, 3, 4].fill(0, 3..1), [1, 2].fill(0, nil), [1, 2].fill(0, 1, 5)
begin; [1].freeze << 1; rescue FrozenError => e; p e.message; end
begin; %w[a b].freeze << 1; rescue FrozenError => e; p e.message; end
begin; a = [1, 2]; a["x"] = 1; rescue TypeError => e; p e.message; end
begin; a = [1, 2]; a[:x] = 1; rescue TypeError => e; p e.message; end
begin; a = [1, 2]; a[nil] = 1; rescue TypeError => e; p e.message; end
begin; a = [1, 2]; a[[0]] = 1; rescue TypeError => e; p e.message; end
a = [1, 2]; a[1.9] = 7; p a
