# A kernel with no entry: a script calls its methods by name

p Bare.fib(20)
t = Bare.tally(3)
p t.class, t.add(4), t.n
u = Bare::Tally.new(10)
p u.add(1)
begin
  Bare.fib
rescue ArgumentError => e
  p e.class
end
begin
  Bare.fib("x")
rescue TypeError => e
  p e.class
end
