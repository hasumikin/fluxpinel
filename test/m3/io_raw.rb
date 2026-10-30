# IO#raw, IO#raw! and IO#noecho of io/console: the test has no terminal on
# its handles, so each raises Errno::ENOTTY, as CRuby does, and the block
# does not run. respond_to? sees them after the require only.
p $stdin.respond_to?(:raw), $stdin.respond_to?(:noecho)
require "io/console"
p $stdin.respond_to?(:raw), $stdin.respond_to?(:raw!), $stdin.respond_to?(:noecho)
ran = false
tries = [
  -> { $stdin.raw { ran = true } },
  -> { $stdin.raw(intr: true, min: 0, time: 0) { ran = true } },
  -> { $stdin.raw!(intr: true) },
  -> { $stdin.noecho { ran = true } },
  -> { File.open(__FILE__) { |f| f.raw { ran = true } } },
]
tries.each do |t|
  begin
    t.call
    puts "set"
  rescue SystemCallError => e
    puts e.class
  end
end
p ran
