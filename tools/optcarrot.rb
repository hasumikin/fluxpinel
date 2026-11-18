# optcarrot.rb -- run optcarrot under CRuby and Fluxpinel, and compare.
#
# Usage: ruby tools/optcarrot.rb --dir DIR --fluxpinel BIN [--frames N]
#                                [--timeout SEC]
#
# Each side runs `bin/optcarrot -b --frames N examples/Lan_Master.nes`
# in DIR, a checkout of mame/optcarrot. -b turns on the checksum and the
# fps. The checksum of Fluxpinel must be the checksum of CRuby. The
# script prints the fps of each side and exits with 1 when the checksums
# differ or Fluxpinel fails.

require "optparse"
require_relative "run_limited"

opt = { frames: 180, timeout: 1800 }
OptionParser.new do |o|
  o.on("--dir DIR") { |v| opt[:dir] = File.expand_path(v) }
  o.on("--fluxpinel BIN") { |v| opt[:fluxpinel] = File.expand_path(v) }
  o.on("--frames N", Integer) { |v| opt[:frames] = v }
  o.on("--timeout SEC", Integer) { |v| opt[:timeout] = v }
end.parse!
abort "optcarrot.rb: give --dir and --fluxpinel" unless opt[:dir] && opt[:fluxpinel]

ARGS = ["bin/optcarrot", "-b", "--frames", opt[:frames].to_s, "examples/Lan_Master.nes"]

# The stdout, the stderr, the status and the seconds of one side. A side
# that runs longer than limit stops, with the status 124.
def run(cmd, dir, limit)
  t = Process.clock_gettime(Process::CLOCK_MONOTONIC)
  out, err, st = RunLimited.capture3({}, cmd, limit: limit, chdir: dir)
  err += "timeout after #{limit} s\n" if st == RunLimited::TIMED_OUT
  [out, err, st, Process.clock_gettime(Process::CLOCK_MONOTONIC) - t]
end

def field(out, name) = out[/^#{name}: (\S+)$/, 1]

# optcarrot prints no fps for a short run.
def fps(out) = field(out, "fps")&.to_f&.round(1) || "-"

ruby = run([RbConfig.ruby, *ARGS], opt[:dir], opt[:timeout])
abort "optcarrot.rb: CRuby fails\n#{ruby[1]}" unless ruby[2] == 0
fx = run([opt[:fluxpinel], *ARGS], opt[:dir], opt[:timeout])

want = field(ruby[0], "checksum")
got = field(fx[0], "checksum")
printf("CRuby:     fps %-8s checksum %s (%.1f s)\n", fps(ruby[0]), want, ruby[3])
printf("Fluxpinel: fps %-8s checksum %s (%.1f s)\n", fps(fx[0]), got || "-", fx[3])
if fx[2] == 0 && got == want
  puts "optcarrot: OK"
else
  $stderr.puts fx[1].lines.last(10).join
  puts "optcarrot: FAIL"
  exit 1
end
