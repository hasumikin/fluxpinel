# The block parameter of a Proc takes the block that call got, and nil
# when call got none. A yield in the body goes to the block of the method
# that made the Proc.

def home
  pr = proc { |a, &blk| blk ? blk.call(a) : :noblk }
  [pr.call(1), pr.call(1) { |x| x + 10 }, proc { |&b| b }.call.nil?, proc { yield 5 }.call]
end
p home { |v| v * 100 }

pr = proc { |a, &blk| blk ? blk.call(a) : :noblk }
p pr.call(1) { |x| x + 1 }, pr.(2) { |x| x + 1 }, pr.yield(3) { |x| x + 1 }, pr[4]

l = ->(a, &blk) { blk ? blk.call(a) : :noblk }
p l.call(5) { |x| x * 2 }, l.(2) { |x| x * 3 }, l[2]

# the block goes on to a call in the body
each_twice = proc { |a, &blk| a.each(&blk); a.each(&blk) }
acc = []
each_twice.call([1, 2]) { |v| acc << v }
p acc

# a method that define_method made takes the block of its call
class Holder
  define_method(:dm) { |a, &blk| blk ? blk.call(a) : :noblk }
end
p Holder.new.dm(1), Holder.new.dm(1) { |x| x + 1 }

# a Proc that a method yields to gets no block
p [1, 2].map(&proc { |a, &b| b.nil? })

# a wrapper that passes the call on, as a hook chain does
chain = proc { |x, &inner| "<#{inner.call(x)}>" }
p chain.call(3) { |v| v * 2 }
