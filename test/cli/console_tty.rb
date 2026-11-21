# IO.console under a pseudo terminal: the open of /dev/tty, which a run
# with no terminal never reaches. The program under the pty runs with the
# checks of the collector on, so a File it does not hold itself has to
# survive a collection. The output is its stdout, without the carriage
# returns of the terminal.
require "pty"
program = <<~RUBY
  require "io/console"
  c = IO.console
  p [c.class, IO.console.equal?(c), c.winsize.size, c.sync, c.path]
  p IO.console(:winsize).size
  def size = IO.console.winsize.size
  a = (1..2000).map { |i| "s\#{i}" }
  GC.start
  b = (1..50).map { File.open("/dev/null") }
  p size, a.size, b.size
RUBY
ENV["SPINEL_GC_STRESS"] = "2"
ENV["SPINEL_GC_VERIFY"] = "1"
out = +""
r, w, pid = PTY.spawn(ENV["FLUXPINEL"], "-e", program)
begin
  out << r.read
rescue Errno::EIO
end
Process.wait(pid)
out << "status #{$?.exitstatus}\n"
print out.gsub("\r", "")
