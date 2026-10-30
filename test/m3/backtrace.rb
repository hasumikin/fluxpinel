# The backtrace of an exception: the frames of Ruby, innermost first,
# and Kernel#caller. The frames of the builtin methods of CRuby are not
# in it, so the test raises from methods and blocks of Ruby only.
class Shop
  def self.open = new.serve
  def serve = twice { fail_here }
  def twice
    yield
    yield
  end
  def fail_here = raise(ArgumentError, "closed")
end

begin
  Shop.open
rescue ArgumentError => e
  puts e.backtrace
end

# a raise by the runtime (a missing method) has a backtrace too
def ask(o) = o.nothing
begin
  ask(1)
rescue NoMethodError => e
  puts e.backtrace
end

# a caught exception raised again keeps its backtrace
def pass
  yield
rescue => e
  raise e
end
begin
  pass { raise "inner" }
rescue => e
  puts e.backtrace
end

# blocks in blocks, and caller
def once
  yield
end
def deep
  once do
    once do
      puts caller(0, 2)
      raise "deep"
    end
  end
end
begin
  deep
rescue => e
  puts e.backtrace.first(3)
end

module Store
  class << self
    def here = caller(0, 1)
  end
end
puts Store.here
p caller(100), caller(0).size
e = RuntimeError.new("fresh")
p e.backtrace
