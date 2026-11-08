# Random: an sp_Random of the runtime; Kernel#rand and srand draw from the
# default instance. The sequence is not CRuby's, so the test prints the
# classes and bounds of the draws, and the equality of two seeded streams.
r = Random.new(1)
p r.class, r.seed, r.rand(10).class, r.rand.class, r.rand(2.5).class
p r.rand(3..7).between?(3, 7), r.rand(3...7).between?(3, 6), r.rand(1.5..2.5).class
p r.bytes(4).bytesize, r == Random.new(1), r.dup == r, Random.new(1) == Random.new(1)
p r.inspect.start_with?("#<Random:0x"), Random.new(1.9).seed, Random.new.class
begin; r.rand(0); rescue ArgumentError => e; p e.message; end
begin; Random.rand(nil); rescue ArgumentError => e; p e.message; end
begin; srand(nil); rescue TypeError => e; p e.message; end

srand(5)
a = [rand(100), rand, rand(1..6), rand(-3)]
srand(5)
b = [rand(100), rand, rand(1..6), rand(-3)]
p a == b, a[0].class, a[1].class, a[2].between?(1, 6), a[3].class, rand(0).class, rand(nil).class
p srand(9), srand.class, Random.srand(3).class, Random.srand(4)
p Random.rand(5).class, Random.rand.class, Random.bytes(3).bytesize, Random.new_seed.class
p Random.urandom(5).bytesize, Random.seed.class

x = (1..10).to_a
p x.shuffle(random: Random.new(5)) == x.shuffle(random: Random.new(5)), x.shuffle(random: Random.new(5)).sort == x
y = x.dup
y.shuffle!(random: Random.new(4))
p y.sort == x, x.sample(random: Random.new(6)) == x.sample(random: Random.new(6))
p x.sample(3, random: Random.new(2)).size, [].sample(random: Random.new(1))

class Random
  def pick(n = 10) = rand(n)
end
p Random.new(1).pick.class, Random.new(1).pick(3).between?(0, 2)
