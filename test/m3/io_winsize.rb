# IO#winsize and IO#winsize= of io/console: the test runs with no terminal
# on its handles, so both raise Errno::ENOTTY, as Reline expects when it
# falls back to a default size.
require "io/console"

[$stdin, $stdout, File.open(__FILE__)].each do |io|
  begin
    io.winsize
    puts "has a size"
  rescue SystemCallError => e
    puts e.class
  end
end
begin
  $stdout.winsize = [24, 80]
rescue SystemCallError => e
  puts e.class
end
begin
  $stdout.winsize = [1]
rescue ArgumentError => e
  puts e.message
end
p $stdout.respond_to?(:winsize)
