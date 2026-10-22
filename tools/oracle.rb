# oracle.rb -- run the spinel test corpus under CRuby, Fluxpinel and AOT.
#
# Usage: ruby tools/oracle.rb --spinel DIR --fluxpinel BIN --out DIR
#                             [--int-overflow promote|raise] [--jobs N] [FILE...]
#
# Both sides run in one integer overflow mode (default promote). Fluxpinel must
# be built in that mode. The corpus follows spinel's own lane for the mode:
# promote leaves out RAISE_MODE_PINS (from spinel's Makefile), raise leaves
# out promote_*.rb.
#
# CRuby's answer is the .expected file of each test (spinel snapshots it from
# CRuby). Fluxpinel runs first. When Fluxpinel runs the program, spinel compiles it and
# the AOT binary runs too. The three stdouts then classify the test:
#
#   agree          Fluxpinel and AOT both give the CRuby answer
#   aot_differs    Fluxpinel gives the CRuby answer, AOT does not: a suspect in
#                  the analysis or the codegen of spinel
#   fx_only      Fluxpinel gives the CRuby answer, spinel refuses the program
#   shared_differs Fluxpinel and AOT give the same wrong answer: a suspect in the
#                  shared runtime, or a deviation that spinel accepts
#   fx_differs   AOT gives the CRuby answer (or spinel refuses), Fluxpinel does not
#   all_differ     all three answers differ
#
# Tests that Fluxpinel cannot run are counted by reason:
#
#   fx_unsupported  the walker has no case for a node (exit status 2)
#   fx_missing      a builtin method is missing (FX_TRACE_MISSING)
#   fx_crash        a signal or a timeout
#
# tools/oracle_known.tsv lists the known defects. A listed test that still
# differs is counted as known, not in its class above. A listed test that
# agrees is counted as known_fixed: its line can go.
#
# The output directory gets results.tsv, report.md and, for each test of an
# interesting class, the outputs under cases/.

require "etc"
require "fileutils"
require "open3"
require "optparse"

opts = { jobs: Etc.nprocessors, timeout: 10, compile_timeout: 120, mode: "promote" }
OptionParser.new do |o|
  o.on("--spinel DIR") { |v| opts[:spinel] = File.expand_path(v) }
  o.on("--int-overflow MODE", %w[promote raise]) { |v| opts[:mode] = v }
  o.on("--fluxpinel BIN") { |v| opts[:fluxpinel] = File.expand_path(v) }
  o.on("--out DIR") { |v| opts[:out] = File.expand_path(v) }
  o.on("--jobs N", Integer) { |v| opts[:jobs] = v }
end.parse!
%i[spinel fluxpinel out].each { |k| abort "oracle: --#{k} is required" unless opts[k] }

SPINEL_DIR = opts[:spinel]
FLUXPINEL = opts[:fluxpinel]
OUT = opts[:out]
FileUtils.rm_rf(OUT)
FileUtils.mkdir_p(File.join(OUT, "bin"))
FileUtils.mkdir_p(File.join(OUT, "cases"))

# ---- which spinel ----

spinel_rev = `git -C #{SPINEL_DIR} rev-parse HEAD`.strip
dirty = `git -C #{SPINEL_DIR} status --porcelain --untracked-files=no`.lines.size
rev_file = File.join(File.dirname(FLUXPINEL), "spinel.rev")
fx_rev = File.exist?(rev_file) ? File.read(rev_file).strip : "(unknown)"
rev_note = fx_rev == spinel_rev ? "same commit" : "DIFFERENT commit: rebuild Fluxpinel"
pin_file = File.expand_path("../SPINEL_COMMIT", __dir__)
pin = File.exist?(pin_file) ? File.read(pin_file).strip : "(unknown)"
pin_note = pin == spinel_rev ? "the pin" : "not the pin #{pin[0, 12]}"
warn "oracle: Fluxpinel was built from #{fx_rev[0, 12]}, the spinel tree is #{spinel_rev[0, 12]}" if fx_rev != spinel_rev
mode_file = File.join(File.dirname(FLUXPINEL), "int_overflow")
fx_mode = File.exist?(mode_file) ? File.read(mode_file).strip.downcase : "(unknown)"
abort "oracle: Fluxpinel is built in #{fx_mode} mode, the oracle runs in #{opts[:mode]}" if fx_mode != opts[:mode]
OVERFLOW_FLAG = "--int-overflow=#{opts[:mode]}"

# ---- the corpus ----

tests = ARGV.empty? ? Dir.glob(File.join(SPINEL_DIR, "test", "*.rb")).sort : ARGV.map { |f| File.expand_path(f) }
if opts[:mode] == "promote"
  pins = File.read(File.join(SPINEL_DIR, "Makefile"))[/^RAISE_MODE_PINS\s*:=(.*)$/, 1].to_s.split
  pins = pins.map { |p| File.join(SPINEL_DIR, p) }
  tests.reject! { |t| pins.include?(t) }
else
  tests.reject! { |t| File.basename(t).start_with?("promote_") }
end
tests.select! { |t| File.exist?("#{t}.expected") }

KNOWN = File.readlines(File.join(__dir__, "oracle_known.tsv"), chomp: true)
            .reject { |l| l.start_with?("#") || l.strip.empty? }
            .to_h { |l| name, what, link = l.split("\t"); [name, "#{what} (#{link})"] }

def norm(s) = s.b.gsub(/\r$/, "")

# Runs cmd in the spinel tree, as spinel's own test run does. Answers
# [stdout, stderr, status]; status is 124 on a timeout, 128 + n on signal n.
def run(cmd, timeout:, stdin: nil, env: {})
  out, err, st = Open3.capture3(env, "timeout", "-k", "2", timeout.to_s, *cmd,
                                chdir: SPINEL_DIR, stdin_data: stdin || "", binmode: true)
  [norm(out), err, st.exitstatus || 128 + st.termsig.to_i]
end

def first_diff(want, got)
  w, g = want.lines, got.lines
  i = (0...[w.size, g.size].max).find { |k| w[k] != g[k] } || 0
  "line #{i + 1}: want #{w[i].inspect}, got #{g[i].inspect}"
end

Result = Struct.new(:test, :klass, :detail)

def check(test, opts)
  name = File.basename(test, ".rb")
  rel = File.join("test", File.basename(test))
  return Result.new(name, :skipped, "has .args") if File.exist?("#{test}.args")
  # a test that spinel has not snapshot yet (work in progress in its tree)
  return Result.new(name, :skipped, "no .expected") unless File.exist?("#{test}.expected")
  stdin =File.exist?("#{test}.stdin") ? File.binread("#{test}.stdin") : nil
  want = norm(File.binread("#{test}.expected"))

  lout, lerr, lst = run([FLUXPINEL, rel], timeout: opts[:timeout], stdin: stdin, env: { "FX_TRACE_MISSING" => "1" })
  if lst == 2 && lerr =~ /^fluxpinel: .*?: unsupported (.*)$/
    return Result.new(name, :fx_unsupported, $1)
  end
  return Result.new(name, :fx_crash, lst == 124 ? "timeout" : "status #{lst}") if lst >= 124
  fx_ok = lout == want
  if !fx_ok && lerr =~ /^fluxpinel: missing builtin (\S+)$/
    return Result.new(name, :fx_missing, $1)
  end

  bin = File.join(OUT, "bin", name)
  _, cerr, cst = run([File.join(SPINEL_DIR, "spinel"), rel, OVERFLOW_FLAG, "-o", bin], timeout: opts[:compile_timeout])
  if cst != 0
    aout = nil
    refusal = cerr.lines.grep(/\S/).last.to_s.strip[0, 160]
  else
    aout, = run([bin], timeout: opts[:timeout], stdin: stdin)
    FileUtils.rm_f(bin)
  end
  aot_ok = aout == want

  klass, detail =
    if fx_ok && aot_ok then [:agree, ""]
    elsif fx_ok && aout.nil? then [:fx_only, refusal]
    elsif fx_ok then [:aot_differs, first_diff(want, aout)]
    elsif aout && aout == lout then [:shared_differs, first_diff(want, lout)]
    elsif aot_ok || aout.nil? then [:fx_differs, first_diff(want, lout)]
    else [:all_differ, first_diff(want, lout)]
    end
  unless %i[agree].include?(klass)
    dir = File.join(OUT, "cases", name)
    FileUtils.mkdir_p(dir)
    File.binwrite(File.join(dir, "expected"), want)
    File.binwrite(File.join(dir, "fluxpinel.out"), lout)
    File.write(File.join(dir, "fluxpinel.err"), lerr)
    File.binwrite(File.join(dir, "aot.out"), aout) if aout
    File.write(File.join(dir, "spinel.err"), cerr) if cst != 0
  end
  Result.new(name, klass, detail)
end

# ---- run ----

queue = Queue.new
tests.each { |t| queue << t }
results = []
lock = Mutex.new
done = 0
workers = Array.new(opts[:jobs]) do
  Thread.new do
    while (t = (queue.pop(true) rescue nil))
      r = check(t, opts)
      if KNOWN.key?(r.test)
        r = Result.new(r.test, r.klass == :agree ? :known_fixed : :known, KNOWN[r.test])
      end
      lock.synchronize do
        results << r
        done += 1
        $stderr.print "\roracle: #{done}/#{tests.size}" if $stderr.tty? && (done % 50 == 0 || done == tests.size)
      end
    end
  end
end
workers.each(&:join)
$stderr.puts
results.sort_by!(&:test)

# ---- report ----

File.write(File.join(OUT, "results.tsv"), results.map { |r| [r.test, r.klass, r.detail].join("\t") }.join("\n") + "\n")

ORDER = %i[agree aot_differs fx_only shared_differs fx_differs all_differ known known_fixed
           fx_unsupported fx_missing fx_crash skipped].freeze
by = results.group_by(&:klass)
ran = results.count do |r|
  %i[agree aot_differs fx_only shared_differs fx_differs all_differ known known_fixed].include?(r.klass)
end

md = +"# Oracle report\n\n"
md << "- spinel: `#{spinel_rev[0, 12]}` (#{pin_note}#{dirty > 0 ? ", #{dirty} modified files" : ""})\n"
md << "- Fluxpinel built from: `#{fx_rev[0, 12]}` (#{rev_note})\n"
md << "- integer overflow: #{opts[:mode]} (Fluxpinel and AOT)\n"
md << "- tests: #{results.size} (Fluxpinel runs #{ran}, #{(100.0 * ran / results.size).round(1)}%)\n\n"
md << "| class | tests |\n|---|---|\n"
ORDER.each { |k| md << "| #{k} | #{(by[k] || []).size} |\n" }

top = lambda do |klass, title|
  rs = by[klass] || []
  return if rs.empty?
  md << "\n## #{title}\n\n| count | #{klass == :fx_missing ? 'method' : 'reason'} |\n|---|---|\n"
  rs.group_by(&:detail).sort_by { |d, v| [-v.size, d] }.first(40).each { |d, v| md << "| #{v.size} | `#{d}` |\n" }
end
top.(:fx_unsupported, "Unsupported in the walker (top 40)")
top.(:fx_missing, "Missing builtin methods (top 40)")

%i[aot_differs fx_only shared_differs fx_differs all_differ known known_fixed fx_crash].each do |k|
  rs = by[k] || []
  next if rs.empty?
  md << "\n## #{k} (#{rs.size})\n\n"
  rs.each { |r| md << "- `#{r.test}`: #{r.detail}\n" }
end
File.write(File.join(OUT, "report.md"), md)
puts md.lines.take_while { |l| !l.start_with?("\n## ") && !l.start_with?("## ") }.join
puts "\nfull report: #{File.join(OUT, 'report.md')}"
