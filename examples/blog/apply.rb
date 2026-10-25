# Changes the Spinel output of the blog of Roundhouse for the console:
#
#   ruby examples/blog/apply.rb BLOG
#
# BLOG is the output directory of `roundhouse --target spinel`. The script
# changes BLOG/main.rb and copies console.rb and kernel/fluxpinel.rb into
# BLOG. Each change finds its anchor line in main.rb. When an anchor is
# not there exactly once, the script stops and changes nothing, so a change
# of the Roundhouse output cannot put a change in the wrong place.

FX = File.expand_path("../..", __dir__)

blog = ARGV[0] or abort "usage: ruby #{$0} BLOG"
main = File.join(blog, "main.rb")
src = File.read(main)
abort "apply: #{main} has the changes already" if src.include?("Fluxpinel demo:")

lines = src.lines

# The index of the line that is text. Stops when not exactly one line is.
def anchor(lines, text)
  found = lines.each_index.select { |i| lines[i].chomp == text }
  return found[0] if found.size == 1
  abort "apply: main.rb has #{found.size} lines `#{text}`, not 1 (see ROUNDHOUSE_COMMIT)"
end

boot = anchor(lines, 'require_relative "boot"')
start = anchor(lines, 'port = (ENV["PORT"] || "3000").to_i')
name = anchor(lines, "Tep::APP.name = Rails.application.global_id_app")
abort "apply: the last line of main.rb is not `end`" unless lines.last.chomp == "end"
abort "apply: the anchors of main.rb are not in order" unless boot < start && start < name

# The server starts in an entry, Main.start, so the host build calls it
# after the top level of the kernel. Insert from the bottom up, so each
# index stays right.
lines.push("nil\n", "end\n", "end\n")
lines.insert(name + 1, <<~RUBY)
  console_port = ENV["FLUXPINEL_CONSOLE"].to_s.to_i
  if console_port > 0
    Thread.new do
      Console.listen(console_port)
    end
  end
RUBY
lines.insert(start, <<~RUBY)
  #
  # Fluxpinel demo: the server starts in an entry, Main.start, so that a
  # host build can run Fluxpinel beside it. FLUXPINEL_CONSOLE=PORT opens
  # the console on 127.0.0.1:PORT.
  module Main
  #: (Array[String]) -> nil
  def self.start(argv)
RUBY
lines.insert(boot + 1, "require_relative \"console\"\n")

File.write(main, lines.join)
File.write(File.join(blog, "console.rb"), File.read(File.join(__dir__, "console.rb")))
File.write(File.join(blog, "fluxpinel.rb"), File.read(File.join(FX, "kernel/fluxpinel.rb")))
puts "apply: changed #{main}"
