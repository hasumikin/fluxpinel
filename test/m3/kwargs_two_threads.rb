# Two threads call methods and blocks with keywords at the same time: each
# call keeps its keywords when the other thread runs between the call and
# the binding of the parameters.
def kw(a, b, c: 1) = a + b + c
def pos(h) = h.size
blk = ->(a, k: 0) { a + k }

bad = 0
threads = 2.times.map do
  Thread.new do
    100_000.times do
      begin
        bad += 1 unless kw(1, 2, c: 3) == 6
        bad += 1 unless blk.call(1, k: 2) == 3
      rescue ArgumentError
        bad += 1
      end
      bad += 1 unless pos({ x: 1 }) == 1
    end
  end
end
threads.each(&:join)
p bad
