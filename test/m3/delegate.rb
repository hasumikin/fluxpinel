require "delegate"
class Stats < SimpleDelegator
  def average = sum / size.to_f
end
s = Stats.new([3, 1, 2])
p s.sort, s.size, s.average, s.__getobj__, s == [3, 1, 2], s != [3, 1], s.eql?(s)
p s.is_a?(Array), s.is_a?(Stats), s.class, s.respond_to?(:push), s.respond_to?(:average), s.respond_to?(:nope)
puts s.to_s, s.inspect, "#{s}"
p s.map { |x| x * 2 }, s.hash == [3, 1, 2].hash, (s <=> [3, 1, 2]), s.methods.include?(:push), !s
s.__setobj__([9])
p s.first, s.average
begin
  s.nope
rescue NoMethodError => e
  puts e.class
end
begin
  s.__setobj__(s)
rescue ArgumentError => e
  puts e.message
end
d = s.dup
d << 10
p s.to_a, d.to_a
f = SimpleDelegator.new("str")
f.freeze
p f.frozen?, f.__getobj__.frozen?, f =~ /t/, f === "str", f.upcase
p Marshal.load(Marshal.dump(SimpleDelegator.new([1, "a"]))).to_a
p SimpleDelegator.ancestors.include?(Delegator), Delegator.instance_method(:method_missing).owner

class Stack < DelegateClass(Array)
  def initialize = super([])
  def peek = last
end
k = Stack.new
k.push(1).push(2)
p k.size, k.peek, k.to_a, k.is_a?(Stack), k.respond_to?(:each), k == [1, 2], k.include?(2)
p Stack.public_instance_methods.include?(:push), Stack.instance_method(:push).owner == Array
p k.inspect, k.map(&:succ)
begin
  Stack.allocate.size
rescue ArgumentError => e
  puts e.message
end
h = DelegateClass(Hash) { def twice(k) = self[k] * 2 }.new({ a: 2 })
p h.twice(:a), h.keys, h.fetch(:a)
