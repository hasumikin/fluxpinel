# bop_report.rb -- check the wrappers of bop_gen and report the coverage.
#
# Usage: ruby bop_report.rb WORKDIR
#
# WORKDIR holds bop_gen_out.c and manifest.tsv from bop_gen. The script:
#
#   1. compiles bop_gen_out.c and maps each error to its wrapper,
#   2. compiles again with the failed wrappers off, and links a program that
#      references every other wrapper, and maps each undefined reference,
#   3. prints the counts per receiver family.
#
# Set SPINEL (spinel checkout) and LUPE_TU_DIR (the directory of lupe_tu.c).
# RT_ARCHIVE, RT_CFLAGS and RT_LIBS name the runtime archive that Lupe
# links and its flags (the threaded one needs -DSP_THREADS and -lpthread).

require "open3"

SPINEL = ENV.fetch("SPINEL", File.expand_path("~/work/spinel"))
TU_DIR = ENV.fetch("LUPE_TU_DIR", File.expand_path("../src", __dir__))
RT_ARCHIVE = ENV.fetch("RT_ARCHIVE", "#{SPINEL}/lib/libspinel_rt.a")
RT_CFLAGS = ENV.fetch("RT_CFLAGS", "").split
RT_LIBS = ENV.fetch("RT_LIBS", "").split
# A type mismatch that C only warns about is a wrong wrapper (a String
# boxed from an sp_StrArray *), so these warnings are errors here.
CFLAGS = %W[-std=gnu11 -O0 -fmax-errors=0 -DSP_INT_OVERFLOW_MODE_RAISE
            -Werror=incompatible-pointer-types -Werror=int-conversion
            -Werror=implicit-function-declaration
            -I#{TU_DIR} -I#{SPINEL}/lib -I#{SPINEL}/lib/regexp] + RT_CFLAGS

Row = Struct.new(:id, :row, :recv, :name, :variant, :status, :reason)

def wrapper_at_line(src_lines)
  starts = []
  src_lines.each_with_index do |l, i|
    starts << [i + 1, $1.to_i] if l =~ /^sp_RbVal lupe_bop_(\d+)\(/
  end
  ->(line) { (starts.select { |s, _| s <= line }.last || [0, nil])[1] }
end

def run(*cmd)
  out, status = Open3.capture2e(*cmd)
  [out, status.success?]
end

dir = ARGV.fetch(0)
gen = File.join(dir, "bop_gen_out.c")
rows = File.readlines(File.join(dir, "manifest.tsv"), chomp: true).map do |l|
  Row.new(*l.split("\t", -1)).tap { |r| r.id = r.id.to_i }
end
find = wrapper_at_line(File.readlines(gen))

# The generated file expects lupe_tu.c in front of it (one TU).
unit = File.join(dir, "gen_unit.c")
File.write(unit, "#include \"lupe_tu.c\"\n#include \"bop_gen_out.c\"\n")

# 1. compile errors
log, = run("cc", *CFLAGS, "-I#{dir}", "-c", unit, "-o", File.join(dir, "gen.o"))
File.write(File.join(dir, "compile.log"), log)
compile_fail = log.scan(/bop_gen_out\.c:(\d+):\d+: error:/).map { |(l)| find.(l.to_i) }.compact.uniq

# 2. link errors, with the failed wrappers off
main_c = File.join(dir, "main.c")
File.write(main_c, <<~C)
  typedef struct { const char *recv, *name, *variant; int argc_min, argc_max;
                   unsigned long long arg0, arg1; void *fn; } LupeBop;
  extern const int lupe_bop_count;
  extern const LupeBop lupe_bop_table[];
  int main(void) { int n = 0; for (int i = 0; i < lupe_bop_count; i++) n += lupe_bop_table[i].fn != 0; return n == 0; }
C
offs = compile_fail.map { |id| "-DLUPE_BOP_OFF_#{id}" }
log, ok = run("cc", *CFLAGS, "-I#{dir}", *offs, "-o", File.join(dir, "link_test"), unit, main_c,
              RT_ARCHIVE, "-lm", "-lcrypt", *RT_LIBS)
File.write(File.join(dir, "link.log"), log)
link_fail = log.scan(/in function `lupe_bop_(\d+)'/).map { |(id)| id.to_i }.uniq
abort "link failed for another reason; see #{dir}/link.log" if !ok && link_fail.empty?
# The flags that turn off every failed wrapper, for a later build.
File.write(File.join(dir, "off.flags"),
           (compile_fail + link_fail).sort.map { |id| "-DLUPE_BOP_OFF_#{id}" }.join(" ") + "\n")

# 3. report
gen_rows = rows.select { |r| r.status == "gen" }
gen_rows.each do |r|
  r.status = "compile-fail" if compile_fail.include?(r.id)
  r.status = "link-fail" if link_fail.include?(r.id)
  r.status = "ok" if r.status == "gen"
end

puts "wrappers (template row x receiver variant)"
gen_rows.group_by(&:status).sort.each { |s, rs| puts format("  %-14s %5d", s, rs.size) }
skips = rows.select { |r| r.status == "skip" }
puts format("  %-14s %5d", "skip", skips.size)
skips.group_by(&:reason).sort_by { |_, rs| -rs.size }.each { |why, rs| puts format("    %-34s %4d", why, rs.size) }

puts
puts "rows by kind"
rows.uniq(&:row).group_by { |r| %w[ok compile-fail link-fail skip].include?(r.status) ? "template" : r.status }
    .sort.each { |s, rs| puts format("  %-14s %5d", s, rs.size) }

# A method counts as covered when one wrapper of it is ok, for one variant.
puts
puts "methods (receiver family, name) with at least one ok wrapper"
puts format("  %-14s %8s %8s %8s", "family", "methods", "covered", "percent")
by_family = rows.reject { |r| r.name.empty? }.group_by(&:recv)
total = covered = 0
by_family.sort_by { |_, rs| -rs.map(&:name).uniq.size }.each do |fam, rs|
  names = rs.map(&:name).uniq
  ok = rs.select { |r| r.status == "ok" }.map(&:name).uniq
  total += names.size
  covered += ok.size
  next if names.size < 10
  puts format("  %-14s %8d %8d %7.0f%%", fam, names.size, ok.size, 100.0 * ok.size / names.size)
end
puts format("  %-14s %8d %8d %7.0f%%", "(all)", total, covered, 100.0 * covered / total)
