# Numeric#step with a step or a limit that is no number raises when the
# walk starts, with a block and with none. min(n), max(n), min_by(n) and
# max_by(n) take a block.

class Two
  def to_int = 2
end

def t
  p yield
rescue => e
  p [e.class, e.message]
end

o = Two.new
t { 1.step(9, o) { } }
t { 1.step(9, "x") { } }
t { 1.step("x", 2) { } }
t { 1.step(9, nil) { } }
t { 1.enum_for(:step, 9, 3).to_a }
t { 1.enum_for(:step, 9, o).to_a }
t { 1.step(9, o).class }
t { 1.step(9, o).to_a }
t { 1.step(9, "x").to_a }
t { 1.step("x", 2).to_a }
t { 1.step("x", 2).first(2) }
t { 1.0.step(9, o).to_a }
t { 1.step(by: o, to: 9).to_a }
t { 1.step(9, nil).to_a }
t { 1.step(nil, o).first(1) }

t { [3, 1, 2].min(2) { |a, b| b <=> a } }
t { [3, 1, 2].max(2) { |a, b| b <=> a } }
t { (1..5).min(2) { |a, b| b <=> a } }
t { (1..5).max(2) { |a, b| b <=> a } }
t { { a: 2, b: 1 }.min(1) { |a, b| a[1] <=> b[1] } }
t { [3, 1, 2].each.min(2) { |a, b| b <=> a } }
t { ["bb", "a", "ccc"].min_by(2) { |s| s.size } }
t { (1..5).min_by(2) { |x| -x } }
t { { a: 2, b: 1 }.max_by(1) { |k, v| v } }
t { [3, 1, 2].min(0) { 0 } }
t { [3, 1, 2].min(-1) { 0 } }
t { [3, 1, 2].max(5) { |a, b| a <=> b } }
t { [3, 1, 2].min("x") { 0 } }
t { [[1, :a], [1, :b], [0, :c]].max(2) { |a, b| a[0] <=> b[0] } }
t { [[1, :a], [1, :b], [0, :c]].min(3) { |a, b| a[0] <=> b[0] } }
t { [3, 1, 2].each_slice(2).min(1) { |a, b| a.sum <=> b.sum } }
