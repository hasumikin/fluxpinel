# console.rb -- run each DIR/*.in under fluxpinel --console and under the
# console of kernel/fluxpinel_cruby.rb, and compare.
#
# Usage: ruby test/console.rb FLUXPINEL DIR...
#
# The input goes to stdin one line at a time, as a socket would give it. A
# test passes when the two stdouts and the two exit statuses are the same.

require "open3"

fluxpinel, *dirs = ARGV
abort "usage: ruby test/console.rb FLUXPINEL DIR..." if fluxpinel.nil? || dirs.empty?
cruby = File.expand_path("../kernel/fluxpinel_cruby.rb", __dir__)

files = dirs.flat_map { |d| Dir.glob(File.join(d, "*.in")).sort }
failed = files.reject do |f|
  input = File.read(f)
  want, wst = Open3.capture2("ruby", "-r", cruby, "-e", "FluxpinelCRuby.console_stdin", stdin_data: input)
  got, gst = Open3.capture2(fluxpinel, "--console", stdin_data: input)
  ok = want == got && wst.exitstatus == gst.exitstatus
  puts "#{ok ? 'ok' : 'NG'} #{f}"
  unless ok
    puts "   status: ruby #{wst.exitstatus}, fluxpinel #{gst.exitstatus}"
    want.lines.zip(got.lines).each_with_index do |(w, g), i|
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
