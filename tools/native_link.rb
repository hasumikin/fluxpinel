# native_link.rb -- the link flags for the native bindings of the packages
# of spinel.
#
# Usage: ruby native_link.rb SPINEL THREADS FFI
#
# A package binds a C function with native_func. The function is in the
# runtime archive or in the object of the package (native_obj). Lupe finds
# it with dlsym when the program calls it. The script prints:
#
#   - the object of each package (sp_x_mt.o when THREADS is 1),
#   - -l for each ffi_lib of a package with an object (openssl: ssl, crypto),
#   - -Wl,-u for each bound function, so that the linker keeps it.
#
# FFI 0 leaves out the packages that need libffi (ffi, fiddle). A package
# whose object is not built is left out with a warning.

spinel, threads, ffi = ARGV
abort "usage: ruby native_link.rb SPINEL THREADS FFI" unless ffi

objs = []
libs = []
syms = []
Dir.glob("#{spinel}/packages/**/*.rb").sort.each do |rb|
  next if rb.include?("/test/")
  pkg = rb.delete_prefix("#{spinel}/packages/").split("/").first
  next if ffi == "0" && %w[ffi fiddle].include?(pkg)
  src = File.read(rb)
  files = src.scan(/^\s*native_obj\s+"([^"]+)"/).flatten.map do |o|
    File.join(spinel, threads == "1" ? o.sub(/\.o\z/, "_mt.o") : o)
  end
  missing = files.reject { |o| File.exist?(o) }
  unless missing.empty?
    warn "native_link.rb: #{missing.join(", ")} is not built; #{pkg} is left out"
    next
  end
  objs.concat(files)
  libs.concat(src.scan(/^\s*ffi_lib\s+"([^"]+)"/).flatten) unless files.empty?
  syms.concat(src.scan(/^\s*native_func\b[^"\n]*"(\w+)"/).flatten)
end

flags = objs.uniq + syms.uniq.map { |s| "-Wl,-u,#{s}" } + libs.uniq.map { |l| "-l#{l}" }
puts flags.join(" ")
