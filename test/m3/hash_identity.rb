# Hash#compare_by_identity: the keys are found by equal?. The runtime
# copies a String key as it stores it (spinel #9171), so the keys here
# are objects and Symbols.
class Key
  def ==(o) = true
  def eql?(o) = true
  def hash = 1
end
h = {}
p h.compare_by_identity?, h.compare_by_identity.equal?(h), h.compare_by_identity?
a = Key.new
h[a] = 1
h[Key.new] = 2
p h.size, h[a], h[Key.new], h.key?(a)
h[:s] = 3
p h[:s], h.compare_by_identity?
g = { Key.new => 1 }
g.compare_by_identity
p g[Key.new], g.size
begin
  {}.freeze.compare_by_identity
rescue FrozenError => e
  p e.class
end
