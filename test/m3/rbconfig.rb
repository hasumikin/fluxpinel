# RbConfig and Gem: the builtins of spinel, loaded at the first read.

p defined?(RbConfig), defined?(Gem)
c = RbConfig::CONFIG
p c["host_os"], c["host_cpu"], c["SOEXT"], c["EXEEXT"], c["arch"], c["MAJOR"]
p RbConfig.ruby.class
p Gem::Version.new("1.2") < Gem::Version.new("1.10"), Gem.win_platform?
p require("rbconfig"), require("rubygems")
