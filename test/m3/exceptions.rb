# Exceptions: raise, begin/rescue/else/ensure, retry and user classes.

def risky(x)
  raise ArgumentError, "bad #{x}" if x.negative?
  raise "plain" if x.zero?
  x * 2
end

[3, -1, 0].each do |x|
  begin
    v = risky(x)
  rescue ArgumentError => e
    puts "arg: #{e.message}"
  rescue => e
    puts "other: #{e.class}: #{e.message}"
  else
    puts "ok #{v}"
  ensure
    puts "ensure #{x}"
  end
end

# a runtime error from a builtin
begin
  1 / 0
rescue ZeroDivisionError => e
  p e.message
end
begin
  nil.upcase
rescue NoMethodError => e
  puts "no method"
end
p((Integer("zz") rescue :fallback))
p(([1, 2].fetch(10) rescue "out"))

# user exception classes
class AppError < StandardError; end
class DbError < AppError
  attr_reader :code

  def initialize(msg = "db failed", code: 1)
    super(msg)
    @code = code
  end
end
begin
  raise DbError.new(code: 7)
rescue AppError => e
  p e.class, e.message, e.code, e.is_a?(StandardError)
end
begin
  raise DbError
rescue DbError => e
  p e.message
end
class Quiet < StandardError
  def message = "custom"
end
begin
  raise Quiet
rescue => e
  p e.message
end
class Named < StandardError
  def initialize(name) = super("missing #{name}")
end
p((raise Named, "x" rescue $!.message))

# retry
tries = 0
begin
  tries += 1
  raise "again" if tries < 3
  p tries
rescue
  retry
end

# ensure runs on return and on a raise that goes on
def with_return
  return :ret
ensure
  puts "ensure in method"
end
p with_return
def nested
  begin
    raise KeyError, "k"
  ensure
    puts "inner ensure"
  end
rescue KeyError => e
  "caught #{e.message}"
end
p nested

# a rescue that does not match lets the exception go on
def pass_through
  begin
    raise TypeError, "t"
  rescue ArgumentError
    :wrong
  end
rescue TypeError
  :right
end
p pass_through

# a bare raise in a rescue raises the same exception again
begin
  begin
    raise IndexError, "first"
  rescue => e
    raise
  end
rescue => e2
  p [e2.class, e2.message, e2.equal?(e)]
end

# rescue a list and a module
module Retryable; end
class Flaky < StandardError
  include Retryable
end
begin
  raise Flaky
rescue Retryable
  puts "by module"
end
begin
  raise TypeError
rescue ArgumentError, TypeError => e
  p e.class
end

# a block frame unwound by a raise
def each_until_raise
  [1, 2, 3].each { |x| raise "stop at #{x}" if x == 2 }
rescue => e
  e.message
end
p each_until_raise
p [1, 2, 3].map { |x| Integer("#{x}x") rescue x }

# the value of begin/rescue
v = begin
  Integer("12")
rescue
  0
end
p v
w = begin
  Integer("no")
rescue
  -1
end
p w
