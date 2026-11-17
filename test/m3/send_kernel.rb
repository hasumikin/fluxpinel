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
