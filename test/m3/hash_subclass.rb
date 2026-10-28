# A subclass of Hash: its instance is a Hash with the methods of the class,
# super into Hash, the default and the default block, ivars, dup, H[...],
# and the methods that answer an instance of the class.

class Registry < Hash
  def initialize(name, default = nil)
    super(default)
    @name = name
  end
  attr_reader :name

  def []=(key, value)
    super(key.to_s, value)
  end

  def [](key) = super(key.to_s)
  def names = keys.sort
end

class Counts < Hash
  def initialize
    super { |h, k| h[k] = 0 }
  end

  def bump(k) = self[k] += 1
end

class Plain < Hash; end

r = Registry.new("reg", :none)
r[:a] = 1
r["b"] = 2
p r, r.class, r.name, r[:a], r["a"], r[:zz], r.names, r.size
p r.is_a?(Hash), r.is_a?(Registry), r.instance_of?(Hash), r.key?("a"), r.fetch("b")
p r.to_h, r.to_h.class, r.to_h.default
d = r.dup
p d.class, d.name, d == r, d.equal?(r), d.default
d["c"] = 3
p d.size, r.size
p r.merge({ "x" => 9 }).class, r.merge({ "x" => 9 }).name, r.merge({ "x" => 9 }).size, r.size
p r.compact.class, r.select { |k, v| v > 1 }.class, r.map { |k, v| v }, r.keys, r.values
p r.each { }.equal?(r), r.each_pair { }.class

c = Counts.new
c.bump(:x)
c.bump(:x)
c.bump(:y)
p c, c.class, c[:zz], c.size

p Plain.new.class, Plain.new(7)[:k], Plain[a: 1], Plain[a: 1].class, Plain[[[:b, 2]]], Plain["k", "v"].class
p Plain.new == {}, Plain[a: 1] == { a: 1 }, { a: 1 } == Plain[a: 1], r.inspect == r.to_h.inspect
h = { r => 1 }
p h[r], r.respond_to?(:names), r.respond_to?(:store), r.frozen?
p r.instance_variables
r.instance_variable_set(:@x, 1)
p r.instance_variables, r.instance_variable_get(:@x)
