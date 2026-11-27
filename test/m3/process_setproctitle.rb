# Process.setproctitle renames the process in the listing of ps, answers the
# title, and leaves $0 alone; Process.argv0 keeps the name the program
# started with
title = "fluxpinel-title-#{$$} one two"
r = Process.setproctitle(title)
puts r == title
puts `ps -o command= -p #{$$}`.strip == title
puts File.basename($0)
puts Process.argv0 == $0, Process.argv0.frozen?

# a title with to_str, and one that is no String
class Title
  def to_str = "from-to_str"
end
puts Process.setproctitle(Title.new)
puts `ps -o command= -p #{$$}`.strip
begin
  Process.setproctitle(42)
rescue TypeError => e
  puts e.message
end

# a title longer than the command line is cut to what fits
long = "x" * 4000
Process.setproctitle(long)
shown = `ps -o command= -p #{$$}`.strip
puts shown.length < long.length, shown.delete("x").empty?
