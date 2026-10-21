# run.rb -- run each test program under CRuby and Fluxpinel and compare.
#
# Usage: ruby test/run.rb [--require FILE.rb]... FLUXPINEL DIR...
#
# A test passes when stdout and the exit status are the same. stderr is not
# compared: the two report an uncaught exception in different words. A
# test with a FILE.expected compares with it instead of CRuby (status 0).
# With --require, CRuby requires each file first: the kernel of the host
# build, as the host build of Fluxpinel has it.

require "open3"

args = ARGV.dup
ruby = ["ruby"]
ruby += ["-r", File.expand_path(args.shift(2).last)] while args.first == "--require"
fluxpinel, *dirs = args
abort "usage: ruby test/run.rb [--require FILE.rb]... FLUXPINEL DIR..." if fluxpinel.nil? || dirs.empty?

def run(*cmd)
  out, _err, st = Open3.capture3(*cmd)
  [out, st.exitstatus]
end

files = dirs.flat_map { |d| Dir.glob(File.join(d, "*.rb")).sort }
failed = files.reject do |f|
  # A program in the dialect of spinel (ffi_func) has no CRuby answer: its
  # answer is FILE.expected, the output of the program that spinel compiles.
  want = File.exist?("#{f}.expected") ? [File.read("#{f}.expected"), 0] : run(*ruby, f)
  got = run(fluxpinel, f)
  ok = want == got
  puts "#{ok ? 'ok' : 'NG'} #{f}"
  unless ok
    puts "   ruby: status #{want[1]}, #{want[0].lines.size} lines"
    puts "   fluxpinel: status #{got[1]}, #{got[0].lines.size} lines"
    want[0].lines.zip(got[0].lines).each_with_index do |(w, g), i|
      next if w == g
      puts "   first diff at line #{i + 1}:"
      puts "     ruby: #{w.inspect}"
      puts "     fluxpinel: #{g.inspect}"
      break
    end
  end
  ok
end
puts "#{files.size - failed.size}/#{files.size} passed"
exit(failed.empty? ? 0 : 1)
