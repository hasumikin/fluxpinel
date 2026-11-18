# A send reaches the private methods of Kernel on any receiver, and a call
# with a receiver does not

Object.new.send(:puts, "via send")
p Object.new.send(:format, "%03d", 5), [1].send(:Integer, "7"), "s".send(:sprintf, "%d", 2)
p((Object.new.send(:raise, "x") rescue $!.message), catch(:t) { 5.send(:throw, :t, 9) })
p((Object.new.puts("x") rescue $!.class), (Object.new.public_send(:puts, "x") rescue $!.class))

class Sender; end
Sender.send(:puts, "on a class")
p Sender.send(:format, "%d", 4), nil.send(:String, 3)

# a method of the receiver comes first
class Printer
  def puts(x) = [:own, x]
end
p Printer.new.send(:puts, 1)
p([3, 1].send(:sort), ([1, 2].send(:select) { |v| v > 1 }))

# method_missing does not get a name that Kernel has
class Catcher
  def method_missing(name, *) = [:missing, name]
end
p Catcher.new.send(:format, "%d", 1), Catcher.new.send(:nope)

# a call with a receiver is a private call: with a block too, and on a
# receiver of any builtin class that has no public method of the name
def refused
  yield
rescue NoMethodError => e
  e.message
end
p refused { 5.fork { } }, refused { Object.new.fork { } }, refused { "s".fork }, refused { [1].fork { } }
p refused { 5.loop { } }, refused { Object.new.catch(:x) { } }, refused { "s".lambda { } }
p refused { [1].at_exit { } }, refused { "s".puts "x" }, refused { { a: 1 }.format("%d", 1) }
p refused { Sender.fork { } }, refused { Sender.new.fork { } }

# a public method of the name is not one
$stdout.puts "IO#puts"
p [1, 2].select { |v| v > 1 }, { a: 1 }.select { |_, v| v > 0 }, Thread.fork { 3 }.value
p File.open(File::NULL) { |f| f.class }, Kernel.format("%d", 3), Random.rand(1)
Point = Struct.new(:test, :format)
p Point.new(1, 2).test, Point.new(1, 2).format, refused { Point.new(1, 2).puts "x" }

# the self of a method of the program reaches fork, and so does a send
class Forker
  def run = fork { exit 5 }
  def self_run = self.fork { exit 6 }
  def self.run = fork { exit 7 }
end
[Forker.new.run, Forker.new.self_run, Forker.run, 5.send(:fork) { exit 8 }, Kernel.fork { exit 9 }].each do |pid|
  Process.wait(pid)
  p $?.exitstatus
end

# method_missing gets a call that the visibility of the method refuses
class Hooked
  def method_missing(name, *, &blk) = [:missing, name, blk ? :block : :none]
  private def secret = 1
  protected def guarded = 2
end
p Hooked.new.fork { }, Hooked.new.loop { }, Hooked.new.puts("x"), Hooked.new.secret, Hooked.new.guarded
p Hooked.new.send(:secret), Hooked.new.send(:format, "%d", 3)
