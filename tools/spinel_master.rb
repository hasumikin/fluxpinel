# spinel_master.rb -- check Fluxpinel on the master of spinel, and find the
# commit of spinel that breaks it.
#
# Usage: ruby tools/spinel_master.rb check --dir DIR --url URL --report FILE [--jobs N]
#        ruby tools/spinel_master.rb step TARGET --dir DIR [--jobs N]
#
# check fetches the master of spinel into DIR (a clone with its history;
# the first run makes it), builds it, builds Fluxpinel on it with no pin
# (SPINEL_PIN=0) under build/master/fx, and runs make all, test, test-host
# and regress in that order. It appends one line to FILE: the date, the
# commit of spinel, and green, or the target that failed. The commit of the
# last green run stays in build/master/good. When a target fails and a
# green commit is known, check bisects spinel between the two with step,
# and the line names the first bad commit.
#
# step is the command of git bisect run: it builds spinel as checked out
# (125 when spinel itself does not build), builds Fluxpinel on it from
# nothing, and runs make TARGET; 0 when it passes, 1 when it fails.
#
# The pinned checkout (SPINEL, deps/spinel) is not touched. The logs of
# the makes are under build/master.

require "fileutils"
require "optparse"
require "time"

ROOT = File.expand_path("..", __dir__)
STATE = File.join(ROOT, "build", "master")
FX_BUILD = File.join(STATE, "fx")

opts = { jobs: 4 }
op = OptionParser.new do |o|
  o.on("--dir DIR") { |v| opts[:dir] = File.expand_path(v) }
  o.on("--url URL") { |v| opts[:url] = v }
  o.on("--report FILE") { |v| opts[:report] = File.expand_path(v) }
  o.on("--jobs N", Integer) { |v| opts[:jobs] = v }
end
mode, *rest = op.parse(ARGV)
abort op.help unless %w[check step].include?(mode) && opts[:dir]

DIR = opts[:dir]
JOBS = opts[:jobs]

# Runs a command with its output in LOG; answers its status.
def run(cmd, log)
  FileUtils.mkdir_p(File.dirname(log))
  system({ "MAKEFLAGS" => nil }, *cmd, out: log, err: log) ? 0 : ($?.exitstatus || 1)
end

def git(*args)
  IO.popen(["git", "-C", DIR, *args], &:read).strip
end

# Builds spinel in DIR; answers whether it built.
def build_spinel(log)
  run(["make", "-C", DIR, "deps"], log) == 0 && run(["make", "-C", DIR, "-j#{JOBS}"], log) == 0
end

# make TARGET of Fluxpinel on the spinel in DIR, from nothing.
def make_fx(target, log)
  FileUtils.rm_rf(FX_BUILD)
  run(["make", "-C", ROOT, "-j#{JOBS}", "SPINEL=#{DIR}", "SPINEL_PIN=0", "BUILD=#{FX_BUILD}", target], log)
end

TARGETS = %w[all test test-host regress].freeze

if mode == "step"
  target = rest[0] or abort op.help
  exit 125 unless build_spinel(File.join(STATE, "step_spinel.log"))
  exit make_fx(target, File.join(STATE, "step_#{target}.log")) == 0 ? 0 : 1
end

abort op.help unless opts[:url] && opts[:report]
FileUtils.mkdir_p(STATE)
unless File.directory?(File.join(DIR, ".git"))
  abort "spinel_master: git clone failed" unless system("git", "clone", "-q", opts[:url], DIR)
end
abort "spinel_master: git fetch failed" unless system("git", "-C", DIR, "fetch", "-q", opts[:url], "master")
system("git", "-C", DIR, "checkout", "-q", "--detach", "FETCH_HEAD")
head = git("rev-parse", "HEAD")
subject = git("log", "-1", "--format=%h %s")
good_file = File.join(STATE, "good")
good = File.exist?(good_file) ? File.read(good_file).strip : nil
stamp = Time.now.strftime("%Y-%m-%d %H:%M")

def report(file, line)
  File.open(file, "a") { |f| f.puts(line) }
  puts line
end

unless build_spinel(File.join(STATE, "spinel.log"))
  report(opts[:report], "- #{stamp} spinel #{subject}: spinel does not build (#{STATE}/spinel.log)")
  exit 1
end
failed = TARGETS.find { |t| make_fx(t, File.join(STATE, "#{t}.log")) != 0 }
if failed.nil?
  File.write(good_file, head + "\n")
  report(opts[:report], "- #{stamp} spinel #{subject}: green")
  exit 0
end
line = "- #{stamp} spinel #{subject}: red at make #{failed} (#{STATE}/#{failed}.log)"
if good && good != head && git("merge-base", "--is-ancestor", good, head) && $?.success?
  system("git", "-C", DIR, "bisect", "start", head, good, out: File::NULL)
  bisect_log = File.join(STATE, "bisect.log")
  run(["git", "-C", DIR, "bisect", "run", "ruby", __FILE__, "step", failed, "--dir", DIR, "--jobs", JOBS.to_s],
      bisect_log)
  culprit = File.read(bisect_log)[/^([0-9a-f]{40}) is the first bad commit/, 1]
  system("git", "-C", DIR, "bisect", "reset", out: File::NULL, err: File::NULL)
  system("git", "-C", DIR, "checkout", "-q", "--detach", head)
  line += culprit ? "; first bad: #{git("log", "-1", "--format=%h %s", culprit)}" : "; bisect gave no commit (#{bisect_log})"
else
  line += "; no green commit to bisect from" unless good
end
report(opts[:report], line)
exit 1
