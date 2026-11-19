# The gems of CRuby that the build and the tests of Fluxpinel read.
# `make gems` installs them. Fluxpinel does not run under Bundler: make
# and the tests call ruby with no `bundle exec`, and tools/gem_dirs.rb
# takes the newest version of each gem that this Ruby has.

source "https://rubygems.org"

# --irb runs these gems under Fluxpinel (tools/gem_dirs.rb). The irb that
# CRuby bundles parses with ripper, which Fluxpinel does not have; irb
# 1.18 and later parse with prism. The default gem of prism has no lib
# directory of its own, so the gem is needed too.
gem "irb", ">= 1.18"
gem "reline"
gem "prism"
gem "rdoc"

# test/m3/ffi_gem.rb compares with the ffi gem under CRuby.
gem "ffi"
