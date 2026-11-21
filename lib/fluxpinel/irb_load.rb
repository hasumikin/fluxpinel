# irb_load.rb -- loads IRB for a session (--irb, --irb-port), or says what
# the build needs.
#
# Fluxpinel runs the irb gem of the CRuby that built it
# (tools/gem_dirs.rb). The irb that CRuby bundles parses with ripper, which
# Fluxpinel does not have. irb 1.18 and later parse with prism. When the
# require stops at irb, ripper or prism, the message names the gems and
# `make gems`, and the process exits with status 1. An other LoadError
# goes on as it is.

begin
  require "irb"
rescue LoadError => e
  raise unless ["irb", "ripper", "prism"].include?(e.path)
  warn "fluxpinel: IRB does not load (#{e.message})."
  warn "IRB on Fluxpinel needs the gems irb (1.18 or later), reline, prism and rdoc."
  warn "Run `make gems` and then `make` in the directory of Fluxpinel, or name the gems with -I or RUBYLIB."
  exit 1
end
