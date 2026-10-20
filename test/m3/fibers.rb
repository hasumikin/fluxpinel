# Fiber, Thread, Enumerator.new and the thread primitives.

def t
  yield
rescue => e
  puts "#{e.class}: #{e.message}"
end

# Fiber: resume passes values in, Fiber.yield passes them out
f = Fiber.new do |a, b|
  puts "start #{a} #{b}"
  x = Fiber.yield(a + b)
  puts "got #{x}"
  y = Fiber.yield(x * 2)
  puts "got #{y}"
  :done
end
p f.resume(1, 2)
p f.resume(10)
p f.alive?
p f.resume(:last)
p f.alive?
t { f.resume }

# a block of the caller runs inside the fiber, and its locals are shared
total = 0
adder = Fiber.new do
  3.times do |i|
    total += i
    Fiber.yield total
  end
  nil
end
4.times { p adder.resume }
p total

# an exception in a fiber goes to the resumer
bad = Fiber.new { raise ArgumentError, "inside" }
t { bad.resume }
p bad.alive?

# nested fibers
outer = Fiber.new do
  inner = Fiber.new { Fiber.yield :inner1; :inner2 }
  Fiber.yield inner.resume
  Fiber.yield inner.resume
  :outer
end
3.times { p outer.resume }

# a generator Enumerator: external and internal iteration
fib = Enumerator.new do |y|
  a = 0
  b = 1
  loop do
    y << a
    c = a + b
    a = b
    b = c
  end
end
p fib.take(10)
p fib.next, fib.next, fib.next, fib.peek
fib.rewind
p fib.next
p fib.first(5)
p fib.find { |x| x > 50 }
p fib.take_while { |x| x < 30 }

squares = Enumerator.new do |y|
  1.upto(4) { |i| y.yield i * i }
  :finished
end
p squares.to_a
p squares.map { |x| x + 1 }
p squares.select(&:even?)
squares.each { |x| print x, " " }
puts
e = squares.dup
4.times { e.next }
t { e.next }
p(loop { e.next })

# Thread: join, value, arguments and shared state
results = []
threads = []
i = 0
while i < 3
  threads << Thread.new(i) do |n|
    results << n * 10
    n + 100
  end
  i += 1
end
p threads.map(&:value)
p 3.times.map { |x| x * 2 }
p results.sort_by { |x| x }
th = Thread.new { Thread.current[:name] = "worker"; Thread.current[:name] }
p th.value

# Queue between threads
q = Queue.new
producer = Thread.new do
  3.times { |i| q << i }
  q << :stop
end
consumed = []
consumer = Thread.new do
  loop do
    v = q.pop
    break if v == :stop
    consumed << v
  end
end
[producer, consumer].each(&:join)
p consumed

# Mutex#synchronize
m = Mutex.new
count = 0
ws = []
ws << Thread.new { 10.times { m.synchronize { count += 1 } } } while ws.size < 4
ws.each(&:join)
p count
p(m.synchronize { :inside })

# an exception in a thread shows at join
Thread.report_on_exception = false
boom = Thread.new { raise "thread failed" }
t { boom.join }
p boom.alive?

p sleep(0)
