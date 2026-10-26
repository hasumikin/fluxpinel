# Kernel#catch and #throw

p catch(:a) { 1 }
p catch(:a) { throw :a; 2 }
p catch(:a) { throw :a, 3; 4 }
p catch(:a) { |t| t }

# the tags: by name, by value and by identity
p catch("s") { |t| throw t, :same }
s = "s"
p catch(s) { throw s, :str }
p catch(1) { throw 1, :int }
p catch(nil) { throw nil, :nil }
p catch(true) { throw true, :true }
p catch { |t| throw t, :auto }
o = Object.new
p catch(o) { throw o, :obj }

# nested: the throw finds its own tag
r = catch(:outer) do
  catch(:inner) do
    throw :outer, :out
  end
  :no
end
p r
p catch(:outer) { [catch(:inner) { throw :inner, 5 }, 6] }

# through a loop, a block and a method
def deep(n)
  throw :done, n if n == 0
  deep(n - 1)
end
p catch(:done) { deep(10) }
p catch(:found) { [1, 2, 3].each { |x| throw :found, x * 10 if x == 2 }; :none }
p catch(:stop) { loop { throw :stop, :looped } }
i = 0
p catch(:w) { while true; i += 1; throw :w, i if i == 3; end }

# ensure clauses run on the way out
log = []
v = catch(:e) do
  begin
    begin
      throw :e, :ensured
    ensure
      log << 1
    end
  ensure
    log << 2
  end
end
p v, log
m = Mutex.new
p catch(:m) { m.synchronize { throw :m, :sync } }
p m.locked?

# a rescue does not take a throw
p(catch(:r) do
  begin
    throw :r, :past
  rescue StandardError
    :rescued
  end
end)

# break, next and return leave a catch
p [1, 2].map { |x| catch(:c) { next x * 2 } }
p [1, 2].each { catch(:c) { break :broke } }
def ret
  catch(:c) { return :returned }
  :no
end
p ret

# a raise leaves a catch, and its tag is gone
begin
  catch(:gone) { raise "x" }
rescue => e
  p e.message
end
begin
  throw :gone
rescue UncaughtThrowError => e
  p e.message, e.tag, e.value
end
begin
  throw :t, 7
rescue ArgumentError => e
  p e.class
rescue UncaughtThrowError => e
  p e.message, e.value
end
p catch(:ok) { 100.times { catch(:x) { raise "y" } rescue nil }; throw :ok, :clean }
