# builtin_census.rb -- count the builtin methods Fluxpinel writes by hand
# against the wrappers it generates from spinel, and fail on a shadow.
#
# Usage: ruby tools/builtin_census.rb MANIFEST [SHADOWED]
#
# MANIFEST is the manifest.tsv of bop_gen. SHADOWED is the shadowed.txt that
# tools/oracle.rb writes from a run of the corpus: each line a call that a
# hand-written method answered while a generated wrapper fits it. The
# builtin layer of Fluxpinel stands in for what spinel lacks (docs/design.md,
# section 3.2.2), so such a method is a second implementation and goes.
# tools/shadowed_known.txt lists the shadows that are known: the work to do.
# The script prints one line of counts and the shadows, and exits 1 on a
# shadow that the list does not name. A listed shadow that the run did not
# find is a line to remove.

manifest, shadowed = ARGV
abort "usage: builtin_census.rb MANIFEST [SHADOWED]" unless manifest
known = File.readlines(File.join(__dir__, "shadowed_known.txt"), chomp: true).reject { |l| l.empty? || l.start_with?("#") }

src = File.expand_path("../src", __dir__)
files = Dir.glob(File.join(src, "*.c"))
hand = files.sum { |f| File.read(f).scan(/FX_IS\("/).size }
hand_files = files.count { |f| File.read(f).include?('FX_IS("') }
prelude = File.read(File.join(src, "prelude.rb")).scan(/^\s*def /).size
gen = File.readlines(manifest).count { |l| l.split("\t")[5] == "gen" }

lines = shadowed && File.exist?(shadowed) ? File.readlines(shadowed, chomp: true).reject(&:empty?) : []
found = lines.map { |l| l.split("\t").first }
new = lines.reject { |l| known.include?(l.split("\t").first) }
puts "census: hand-written FX_IS #{hand} in #{hand_files} files, prelude defs #{prelude}, " \
     "generated wrappers #{gen}, shadowed #{lines.size} (known #{lines.size - new.size}, new #{new.size})"
new.each do |l|
  m, n, tests = l.split("\t")
  puts "census: new shadow #{m} (#{n} tests: #{tests}): remove the hand-written method, or list it in tools/shadowed_known.txt"
end
(known - found).each { |m| puts "census: known shadow #{m} not found: remove its line from tools/shadowed_known.txt" } unless lines.empty?
exit(new.empty? ? 0 : 1)
