# gem_oracle.rb -- load the packages of spinel and the files of some gems
# under Fluxpinel, and report the first failure of each.
#
# Usage: ruby tools/gem_oracle.rb --spinel DIR --fluxpinel BIN --out DIR
#                                 [--gems reline,irb,prism,rdoc] [--jobs N] [NAME...]
#
# The items are require names, from three places:
#
#   package  the entry files of the packages of spinel (packages/x/x.rb)
#   gem      the entry file of each gem of --gems (the gems of the CRuby that
#            runs this script)
#   file     any other file of those gems that CRuby loads alone; a file that
#            only loads after its gem (class Reline::X without Reline) is skipped
#
# Fluxpinel gets the lib directory of each gem on -I and requires the name.
# The standard library resolves from the packages of spinel, as a program of
# the spinel ecosystem sees it. Each item gives its first failure only, so
# the report is a list of the next things to make work:
#
#   loads           the require answers
#   fx_unsupported  the walker has no case for a node (exit status 2)
#   fx_missing      a builtin method is missing (FX_TRACE_MISSING)
#   fx_error        the require raises
#   fx_crash        a signal or a timeout
#   skipped         CRuby cannot load the file alone
#
# The output directory gets results.tsv and report.md. The report groups the
# failures by reason, most frequent first.

require "etc"
require "fileutils"
require "open3"
require "optparse"

opts = { jobs: Etc.nprocessors, timeout: 10, gems: %w[reline irb prism rdoc] }
OptionParser.new do |o|
  o.on("--spinel DIR") { |v| opts[:spinel] = File.expand_path(v) }
  o.on("--fluxpinel BIN") { |v| opts[:fluxpinel] = File.expand_path(v) }
  o.on("--out DIR") { |v| opts[:out] = File.expand_path(v) }
  o.on("--gems LIST") { |v| opts[:gems] = v.split(",") }
  o.on("--jobs N", Integer) { |v| opts[:jobs] = v }
end.parse!
%i[spinel fluxpinel out].each { |k| abort "gem_oracle: --#{k} is required" unless opts[k] }

SPINEL_DIR = opts[:spinel]
FLUXPINEL = opts[:fluxpinel]
OUT = opts[:out]
FileUtils.rm_rf(OUT)
FileUtils.mkdir_p(OUT)

spinel_rev = `git -C #{SPINEL_DIR} rev-parse HEAD`.strip
rev_file = File.join(File.dirname(FLUXPINEL), "spinel.rev")
fx_rev = File.exist?(rev_file) ? File.read(rev_file).strip : "(unknown)"
warn "gem_oracle: Fluxpinel was built from #{fx_rev[0, 12]}, the spinel tree is #{spinel_rev[0, 12]}" if fx_rev != spinel_rev

# ---- the items ----

Item = Struct.new(:name, :kind, :gem)

gems = opts[:gems].to_h do |g|
  spec = Gem::Specification.find_by_name(g)
  [g, spec]
rescue Gem::MissingSpecError
  abort "gem_oracle: the gem #{g} is not installed in this CRuby"
end
LIBS = gems.values.map { |s| File.join(s.gem_dir, "lib") }

items = []
Dir.glob(File.join(SPINEL_DIR, "packages", "*", "*.rb")).sort.each do |f|
  next if f.end_with?(".share.rb")
  items << Item.new(File.basename(f, ".rb"), :package, nil)
end
gems.each do |g, spec|
  lib = File.join(spec.gem_dir, "lib")
  Dir.glob(File.join(lib, "**", "*.rb")).sort.each do |f|
    name = f.delete_prefix(lib + "/").delete_suffix(".rb")
    items << Item.new(name, name == g ? :gem : :file, g)
  end
end
items.select! { |i| ARGV.include?(i.name) } unless ARGV.empty?

# ---- run ----

def norm(s) = s.b.gsub(/\r$/, "").gsub(/0x[0-9a-f]+/, "0x..")

# Answers [stderr, status]; status is 124 on a timeout, 128 + n on a signal.
def run(cmd, timeout:, env: {})
  _, err, st = Open3.capture3(env, "timeout", "-k", "2", timeout.to_s, *cmd,
                              chdir: OUT, stdin_data: "", binmode: true)
  [norm(err), st.exitstatus || 128 + st.termsig.to_i]
end

Result = Struct.new(:item, :klass, :detail)

def check(item, opts)
  incl = LIBS.flat_map { |d| ["-I", d] }
  prog = "require #{item.name.inspect}"
  if item.kind == :file
    _, st = run(["ruby", "--disable-gems", *incl, "-e", prog], timeout: opts[:timeout])
    return Result.new(item, :skipped, "CRuby cannot load it alone") if st != 0
  end
  err, st = run([FLUXPINEL, *incl, "-e", prog], timeout: opts[:timeout], env: { "FX_TRACE_MISSING" => "1" })
  return Result.new(item, :loads, "") if st == 0
  return Result.new(item, :fx_crash, st == 124 ? "timeout" : "status #{st}") if st >= 124
  return Result.new(item, :fx_unsupported, $1) if st == 2 && err =~ /^fluxpinel: .*?: unsupported (.*)$/
  return Result.new(item, :fx_missing, $1) if err =~ /^fluxpinel: missing builtin (\S+)$/
  last = err.lines.grep(/\S/).last.to_s.strip
  Result.new(item, :fx_error, last[0, 160])
end

queue = Queue.new
items.each { |i| queue << i }
results = []
lock = Mutex.new
workers = Array.new(opts[:jobs]) do
  Thread.new do
    while (i = (queue.pop(true) rescue nil))
      r = check(i, opts)
      lock.synchronize { results << r }
    end
  end
end
workers.each(&:join)
results.sort_by! { |r| [r.item.kind.to_s, r.item.name] }

# ---- report ----

File.write(File.join(OUT, "results.tsv"),
           results.map { |r| [r.item.name, r.item.kind, r.klass, r.detail].join("\t") }.join("\n") + "\n")

ORDER = %i[loads fx_unsupported fx_missing fx_error fx_crash skipped].freeze
by = results.group_by(&:klass)
ran = results.count { |r| r.klass != :skipped }

md = +"# Gem oracle report\n\n"
md << "- spinel: `#{spinel_rev[0, 12]}`\n"
md << "- Fluxpinel built from: `#{fx_rev[0, 12]}`\n"
md << "- gems: #{gems.map { |g, s| "#{g} #{s.version}" }.join(', ')}\n"
md << "- items: #{results.size} (#{ran} run, #{(by[:loads] || []).size} load)\n\n"
md << "| class | items |\n|---|---|\n"
ORDER.each { |k| md << "| #{k} | #{(by[k] || []).size} |\n" }

failed = results.select { |r| %i[fx_unsupported fx_missing fx_error fx_crash].include?(r.klass) }
md << "\n## Reasons (top 40)\n\n| count | class | reason | items |\n|---|---|---|---|\n"
failed.group_by { |r| [r.klass, r.detail] }.sort_by { |(k, d), v| [-v.size, k.to_s, d] }.first(40).each do |(k, d), v|
  names = v.map { |r| r.item.name }
  shown = names.first(6).join(", ") + (names.size > 6 ? ", ... (#{names.size})" : "")
  md << "| #{v.size} | #{k} | `#{d}` | #{shown} |\n"
end
puts md

ORDER.each do |k|
  rs = by[k] || []
  next if rs.empty?
  md << "\n## #{k} (#{rs.size})\n\n"
  rs.each { |r| md << "- `#{r.item.name}` (#{r.item.kind})#{r.detail.empty? ? '' : ": #{r.detail}"}\n" }
end
File.write(File.join(OUT, "report.md"), md)
puts "full report: #{File.join(OUT, 'report.md')}"
