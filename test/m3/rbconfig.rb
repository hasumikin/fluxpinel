# RbConfig and Gem: the builtins of spinel, loaded at the first read.

p defined?(RbConfig), defined?(Gem)
c = RbConfig::CONFIG
# the name of the system with no version: CRuby on macOS has "darwin23",
# and spinel has "darwin"
p c["host_os"][/\A[a-z]+/], c["host_cpu"], c["SOEXT"], c["EXEEXT"], c["arch"][/\A\w+-[a-z]+/], c["MAJOR"]
p RbConfig.ruby.class
p Gem::Version.new("1.2") < Gem::Version.new("1.10"), Gem.win_platform?
p require("rbconfig"), require("rubygems")
# RbConfig.ruby: the running interpreter, which runs a program
r = RbConfig.ruby
p r.start_with?("/"), File.executable?(r)
p IO.popen([r, "-e", "puts 6 * 7"], &:read)
