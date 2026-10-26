# IO.select, Kernel#select and the waits of an IO, on a pipe

r, w = IO.pipe
p IO.select([r], nil, nil, 0)
p IO.select(nil, [w], nil, 0) == [[], [w], []]
w.write "x"
ready = IO.select([r], [w], nil, 1)
p ready[0] == [r], ready[1] == [w]
p select([r], nil, nil, 0)[0].size, Kernel.select([r], nil, nil, 0)[0].size
p r.read(1)

p r.wait_readable(0), w.wait_writable(0) == w
w.write "y"
p r.wait_readable(1) == r, r.wait(1) == r, w.wait(0, :write) == w
p r.read(1)

# an object with to_io waits as its IO
class Wrap
  def initialize(io) = (@io = io)
  def to_io = @io
end
wr = Wrap.new(r)
w.write "z"
p IO.select([wr], nil, nil, 1)[0][0].equal?(wr)
p r.read(1)

[-> { IO.select(r) }, -> { IO.select([1], nil, nil, 0) }, -> { IO.select([r], nil, nil, -1) }].each do |l|
  l.call
rescue TypeError, ArgumentError => e
  p e.class
end
r.close
w.close
