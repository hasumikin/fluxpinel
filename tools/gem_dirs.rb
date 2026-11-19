# gem_dirs.rb -- the lib directories of the gems that --irb needs, as a
# C header (build/gem_dirs.h).
#
# Usage: ruby tools/gem_dirs.rb GEM...
#
# IRB and Reline are pure Ruby, so Fluxpinel runs the ones of the CRuby
# that builds it. FX_GEM_DIRS holds their lib directories, joined by ":",
# and --irb adds them at the end of $LOAD_PATH (src/main.c). A gem that
# this Ruby does not have, or has only in a version that --irb cannot run,
# prints a warning and stays out: --irb then needs -I or RUBYLIB. With no
# gem found, the header defines nothing. The directories of GEM_DIRS_FIRST
# ("a:b") come before those of the gems.
#
# The versions that --irb cannot run:
# - irb before 1.18 parses with ripper, which Fluxpinel does not have.
# - The default gem of prism has no lib directory of its own.
# - The prism gem without build/libprism.so: IRB loads prism through its
#   ffi backend, which opens that file.

MIN_VERSION = { "irb" => "1.18" }.freeze

# Why --irb cannot run the gem of spec, or nil.
def unusable(name, spec)
  min = MIN_VERSION[name]
  return "is older than #{min}" if min && spec.version < Gem::Version.new(min)
  return "has no lib directory" unless Dir.exist?(File.join(spec.full_gem_path, "lib"))
  return "has no build/libprism.so" if name == "prism" && !File.exist?(File.join(spec.full_gem_path, "build/libprism.so"))
  nil
end

first = ENV.fetch("GEM_DIRS_FIRST", "").split(":").reject(&:empty?)
problems = []
dirs = first + ARGV.filter_map do |name|
  spec = Gem::Specification.find_by_name(name)
  why = unusable(name, spec)
  problems << "#{name} #{spec.version} #{why}" if why
  dir = File.join(spec.full_gem_path, "lib")
  dir if Dir.exist?(dir)
rescue Gem::MissingSpecError
  problems << "no gem #{name}"
  nil
end
unless problems.empty?
  warn "gem_dirs.rb: --irb of this build will not start with Ruby #{RUBY_VERSION}: #{problems.join('; ')}."
  warn "gem_dirs.rb: Run `make gems`."
end

puts "/* the lib directories of the gems of --irb (tools/gem_dirs.rb) */"
puts "#define FX_GEM_DIRS #{dirs.join(':').inspect}" unless dirs.empty?
