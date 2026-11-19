# irb_tty.rb -- run `fluxpinel --irb` on a pseudo terminal and type as a
# user does: the path through the line editor of Reline, which the
# sessions of test/irb (no terminal) do not take.
#
# Usage: ruby test/irb_tty.rb FLUXPINEL
#
# Each step types keys and waits for a text on the screen. The screen is
# what the terminal got, without the escape sequences (colors, cursor
# moves). A step fails when its text does not come in STEP_LIMIT seconds;
# the last lines of the screen then show.

require "pty"
require "io/console"
require "tmpdir"

fluxpinel = ARGV[0] or abort "usage: ruby test/irb_tty.rb FLUXPINEL"
fluxpinel = File.expand_path(fluxpinel)
STEP_LIMIT = 20

# [what the step checks, the keys, the text that the screen must get]
STEPS = [
  ["the prompt", "", "irb(main):001> "],
  ["a line gives its value", "1 + 1\r", "=> 2\r\n"],
  ["an open def waits for its end", "def twice(x)\r", "irb(main):003* "],
  ["the end of the def gives its name", "x * 2\rend\r", "=> :twice\r\n"],
  ["a method of the session answers", "twice(21)\r", "=> 42\r\n"],
  ["Tab lists the methods of the receiver", "[1, 2].ma\t", "max_by"],
  ["Ctrl-C drops the line", "\x03", "^C\r\n"],
  ["the up arrow brings the last line back", "\e[A", "twice(21)"],
  ["Enter runs the line of the history", "\r", "=> 42\r\nirb(main):007> "],
  ["an error shows and the session goes on", "[1].frist\r", "(NoMethodError)"],
  ["the line after an error gives its value", "2 * 3\r", "=> 6\r\n"]
].freeze

failed = nil
status = nil
Dir.mktmpdir do |home|
  # no ~/.irbrc and no history file of the user; a terminal of 80 columns
  env = { "HOME" => home, "XDG_CONFIG_HOME" => home, "IRBRC" => nil, "TERM" => "xterm-256color" }
  PTY.spawn(env, fluxpinel, "--irb", "-f") do |r, w, pid|
    w.winsize = [24, 80]
    seen = "".b
    plain = -> { seen.gsub(/\e\[[0-9;?]*[A-Za-z]/n, "".b).gsub(/\e[=>]/n, "".b) }
    from = 0   # a step reads the screen after the text of the step before
    STEPS.each do |what, keys, text|
      w.write(keys)
      deadline = Time.now + STEP_LIMIT
      at = nil
      until (at = plain.call.index(text.b, from))
        break if Time.now > deadline
        next unless r.wait_readable(0.1)
        begin
          seen << r.readpartial(4096)
        rescue EOFError, Errno::EIO
          break   # the program ended
        end
      end
      puts "#{at ? 'ok' : 'NG'} #{what}"
      unless at
        failed = what
        puts "   the screen has no #{text.inspect}; its last lines:"
        plain.call.force_encoding("UTF-8").scrub.lines.last(8).each { |l| puts "     #{l.inspect}" }
        break
      end
      from = at + text.bytesize
    end
    w.write("\x03") if failed
    w.write("exit\r")
    begin
      r.read
    rescue Errno::EIO
      # Linux: the read of a pty whose program ended
    end
    _, st = Process.wait2(pid)
    status = st.exitstatus
  end
end
unless failed
  ok = status == 0
  puts "#{ok ? 'ok' : 'NG'} exit ends the program with status 0#{ok ? '' : " (got #{status.inspect})"}"
  failed = "exit" unless ok
end
total = STEPS.size + 1
puts failed ? "irb_tty: failed at #{failed.inspect}" : "#{total}/#{total} passed"
exit(failed ? 1 : 0)
