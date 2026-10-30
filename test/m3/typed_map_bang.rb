# map!, select! and reject! on an Array that the runtime keeps typed
# (the Strings of String#chars, the Integers of a literal).
c = "a\tb".chars
c.map! { |g| g == "\t" ? "  " : g }
p c, c.join
w = %w[one two three]
p w.select! { |s| s.size == 3 }, w
p w.reject! { |s| s.size == 3 }, w
n = [1, 2, 3]
n.map! { |x| x * x }
p n
p n.reject! { |x| x > 100 }
begin
  "ab".chars.freeze.map! { |g| g }
rescue FrozenError => e
  p e.class
end
