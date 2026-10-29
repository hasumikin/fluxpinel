# The builtin methods that the start of irb asks for: the access
# predicates of File, Dir.pwd, IO.open on a descriptor, Kernel.load from a
# method named load, the backtrace of an exception, and the forms of
# reverse_each and any? with no block.

p File.readable?(__FILE__), File.writable?(__FILE__), File.executable?(__FILE__), File.readable?("/no/such")
p File.readable?("/"), File.executable?("/"), File.writable?("/no/such")
p File.respond_to?(:readable?), File.respond_to?(:writable?)
p Dir.pwd == File.expand_path("."), Dir.getwd == Dir.pwd, Dir.home == ENV["HOME"]

$stdout.flush
out = IO.open(1, "w", external_encoding: "UTF-8", internal_encoding: "-")
out.puts "through IO.open"
out.flush
p IO.for_fd(1, "w").class, IO.new(2, "w").class
inp = IO.open(0)
p inp.class

# Kernel.load and Kernel.require from a method of the name
class Loader
  def load(x) = ::Kernel.load(x)
  def require(x) = Kernel.require(x)
end
path = File.join(File.dirname(__FILE__), "kernel_misc_loaded.rb")
File.write(path, "puts 'loaded once'\n")
begin
  Loader.new.load(path)
  Loader.new.load(path)
  p Loader.new.require("set")
ensure
  File.delete(path)
end

# the backtrace of an exception: nil before set_backtrace, then as set
e = RuntimeError.new("boom")
p e.backtrace, e.backtrace_locations
p e.set_backtrace(["a.rb:1", "b.rb:2"]), e.backtrace
p e.set_backtrace("c.rb:3"), e.set_backtrace(nil), e.backtrace
p e.full_message(highlight: false, order: :top).include?("boom (RuntimeError)")
p e.full_message.include?("boom")

# the enumerator and the predicate forms with no block
p [1, 2, 3].reverse_each.to_a, [].reverse_each.to_a, [4, 5].reverse_each.map { |x| x * 2 }
p [1, nil].any?, [].any?, [nil].any?
p [1, 2].all?, [].all?, [1, nil].all?
p [nil, false].none?, [].none?, [1].none?
p [1, nil].one?, [1, 2].one?, [].one?
