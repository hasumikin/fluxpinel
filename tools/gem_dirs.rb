# gem_dirs.rb -- the lib directories of the gems of the build (those that
# --irb needs), as a C header (build/gem_dirs.h).
#
# Usage: ruby tools/gem_dirs.rb GEM...
#
# IRB and Reline are pure Ruby, so Fluxpinel runs the ones of the CRuby
# that builds it. FX_GEM_DIRS holds their lib directories, joined by ":",
# and every program has them at the end of $LOAD_PATH (src/main.c), so
# that `require "reline"` works with no RUBYLIB. A gem that this Ruby does
# not have, or has only in a version that --irb cannot run, prints a
# warning and stays out: a program, and --irb, then need -I or RUBYLIB. With no
# gem found, the header defines nothing. The directories of GEM_DIRS_FIRST
# ("a:b") come before those of the gems.
#
# The versions that --irb cannot run:
# - irb before 1.18 parses with ripper, which Fluxpinel does not have.
# - The default gem of prism has no lib directory of its own.
# - The prism gem without its shared library (build/libprism.so, or
#   build/libprism.dylib on macOS): IRB loads prism through its ffi
#   backend, which opens that file.

require "rbconfig"

MIN_VERSION = { "irb" => "1.18" }.freeze

# The shared library of the prism gem, from the directory of the gem.
LIBPRISM = "build/libprism.#{RbConfig::CONFIG['SOEXT']}".freeze

# Why --irb cannot run the gem of spec, or nil.
def unusable(name, spec)
  min = MIN_VERSION[name]
  return "is older than #{min}" if min && spec.version < Gem::Version.new(min)
  return "has no lib directory" unless Dir.exist?(File.join(spec.full_gem_path, "lib"))
  return "has no #{LIBPRISM}" if name == "prism" && !File.exist?(File.join(spec.full_gem_path, LIBPRISM))
  nil
end

# The gems that --irb starts without. did_you_mean is a default gem of
# CRuby: its files are in the directory of the standard library until the
# gem is installed too, and that directory cannot be on the load path of
# Fluxpinel.
OPTIONAL = { "did_you_mean" => "an error has no \"Did you mean?\" line" }.freeze

first = ENV.fetch("GEM_DIRS_FIRST", "").split(":").reject(&:empty?)
problems = []
missing = []
dirs = first + ARGV.filter_map do |name|
  spec = Gem::Specification.find_by_name(name)
  why = unusable(name, spec)
  (OPTIONAL[name] ? missing : problems) << "#{name} #{spec.version} #{why}" if why
  dir = File.join(spec.full_gem_path, "lib")
  dir if Dir.exist?(dir)
rescue Gem::MissingSpecError
  (OPTIONAL[name] ? missing : problems) << "no gem #{name}"
  nil
end
unless problems.empty?
  warn "gem_dirs.rb: --irb of this build will not start with Ruby #{RUBY_VERSION}: #{problems.join('; ')}."
end
unless missing.empty?
  warn "gem_dirs.rb: --irb of this build has less with Ruby #{RUBY_VERSION}: #{missing.join('; ')} " \
       "(#{OPTIONAL.values_at(*ARGV).compact.join('; ')})."
end
warn "gem_dirs.rb: Run `make gems`." unless problems.empty? && missing.empty?

puts "/* the lib directories of the gems of --irb (tools/gem_dirs.rb) */"
puts "#define FX_GEM_DIRS #{dirs.join(':').inspect}" unless dirs.empty?
