# Kernel#format, #sprintf and #printf, IO#printf, and a module function
# of Kernel called on Kernel

def t(label)
  r = yield
  puts "#{label}: #{r.inspect}"
rescue => e
  puts "#{label}: #{e.class} #{e.message}"
end

t("directives") { format("%05.1f|%s|%-3d|%x|%+e|%%", 2.25, :a, 7, 255, 1234.5) }
t("names") { sprintf("%{a}-%<b>03d", a: 1, b: 2) }
t("width args") { format("%*d|%-*s|", 4, 1, 3, "x") }
t("positions") { format("%2$s %1$s %2$s", "a", "b") }
class Value
  def to_f = 1.25
end
t("to_f") { format("%.1f|%e", Value.new, Value.new) }
t("IO#printf with nothing") { $stdout.printf }
t("an Array") { format("%s", [1, 2]) }
t("plain") { format("plain") }
t("Kernel.format") { Kernel.format("%x", 255) }
t("too few") { format("%d") }
t("too many") { format("%d", 1, 2) }
t("nil") { format(nil) }
t("Integer") { format(5) }
t("bad arg") { format("%d", "x") }
t("printf with nothing") { printf }

printf("%d-%s\n", 1, "a")
$stdout.printf("%x\n", 255)
printf($stdout, "io %d\n", 3)
Kernel.puts "kernel"
require "stringio"
s = StringIO.new
s.printf("%03d", 7)
p s.string

class Sink
  def write(x) = (STDOUT.write("[#{x}]"); x.size)
end
$stdout = Sink.new
printf("%s!", "redirected")
$stdout = STDOUT
puts
