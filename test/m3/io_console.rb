# IO.console: the terminal of the process (/dev/tty), one File answered
# each time, or nil when the process has none.
require "io/console"
c = IO.console
p [NilClass, File].include?(c.class)
p IO.console.equal?(c)
if c
  p c.winsize.size, c.tty?, c.path
  c.winsize = c.winsize
  p c.respond_to?(:raw)
else
  p :no_terminal
end
