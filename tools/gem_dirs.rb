# gem_dirs.rb -- the lib directories of the gems that --irb needs, as a
# C header (build/gem_dirs.h).
#
# Usage: ruby tools/gem_dirs.rb GEM...
#
# IRB and Reline are pure Ruby, so Fluxpinel runs the ones of the CRuby
# that builds it. FX_GEM_DIRS holds their lib directories, joined by ":",
# and --irb adds them at the end of $LOAD_PATH (src/main.c). A gem that
# CRuby does not have prints a warning and stays out: --irb then needs -I
# or RUBYLIB. With no gem found, the header defines nothing. The
# directories of GEM_DIRS_FIRST ("a:b") come before those of the gems.

first = ENV.fetch("GEM_DIRS_FIRST", "").split(":").reject(&:empty?)
dirs = first + ARGV.filter_map do |name|
  spec = Gem::Specification.find_by_name(name)
  dir = File.join(spec.full_gem_path, "lib")
  next dir if Dir.exist?(dir)
  warn "gem_dirs.rb: #{name} #{spec.version} has no lib directory, #{dir}"
  nil
rescue Gem::MissingSpecError
  warn "gem_dirs.rb: no gem #{name}; --irb needs -I or RUBYLIB for it"
  nil
end

puts "/* the lib directories of the gems of --irb (tools/gem_dirs.rb) */"
puts "#define FX_GEM_DIRS #{dirs.join(':').inspect}" unless dirs.empty?
