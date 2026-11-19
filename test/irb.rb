# irb.rb -- run each session of a directory under the IRB of CRuby and
# under `fluxpinel --irb`, and compare what the two print.
#
# Usage: ruby test/irb.rb FLUXPINEL DIR
#
# A session is a file DIR/NAME.txt: the lines that IRB reads from stdin,
# with no terminal. A session passes when the two outputs (stdout, then
# stderr) are the same after these changes:
#
# - An address (0x000071e2...) reads 0xADDR.
# - A time that `measure` prints reads 0.0s.
# - A line of a backtrace after the first ("\tfrom ...") goes. The launcher
#   of each side has frames of its own, and Fluxpinel has no line for the
#   methods that CRuby calls inside a builtin method.
#
# The first difference of a failed session shows with its line number.

require "tmpdir"
require_relative "../tools/run_limited"

fluxpinel, dir = ARGV
abort "usage: ruby test/irb.rb FLUXPINEL DIR" unless fluxpinel && dir
fluxpinel = File.expand_path(fluxpinel)

OPTIONS = %w[-f --nocolorize --nomultiline --nosingleline --simple-prompt].freeze
LIMIT = 120

def normal(text)
  text.lines.reject { |l| l.start_with?("\tfrom ") }.join
      .gsub(/0x\h+(\.\.\.)?/, "0xADDR")
      .gsub(/processing time: [\d.]+s/, "processing time: 0.0s")
end

def session(cmd, input, home)
  env = { "HOME" => home, "NO_COLOR" => "1", "IRBRC" => nil, "XDG_CONFIG_HOME" => home }
  out, err, status = RunLimited.capture3(env, cmd + OPTIONS, limit: LIMIT, stdin_data: input)
  [normal(out + err), status]
end

files = Dir[File.join(dir, "*.txt")].sort
abort "irb.rb: no session in #{dir}" if files.empty?
failed = files.reject do |file|
  input = File.read(file)
  want = got = nil
  Dir.mktmpdir do |home|
    want = session(%w[ruby -S irb], input, home)
    got = session([fluxpinel, "--irb"], input, home)
  end
  ok = want == got
  puts "#{ok ? 'ok' : 'NG'} #{file}"
  unless ok
    puts "   status: irb #{want[1]}, fluxpinel #{got[1]}" unless want[1] == got[1]
    a = want[0].lines
    b = got[0].lines
    i = (0...[a.size, b.size].max).find { |n| a[n] != b[n] }
    if i
      puts "   first diff at line #{i + 1}:"
      puts "     irb: #{a[i].inspect}"
      puts "     fluxpinel: #{b[i].inspect}"
    end
  end
  ok
end
puts "#{files.size - failed.size}/#{files.size} passed"
exit(failed.empty? ? 0 : 1)
