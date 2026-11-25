# io/nonblock (lib/io/nonblock.rb): IO#nonblock?, IO#nonblock= and
# IO#nonblock with a block. StringScanner::Version and Id
# (lib/fluxpinel/strscan.rb).
p require("io/nonblock")
r, w = IO.pipe
r.nonblock = false
p r.nonblock?, (r.nonblock = true), r.nonblock?
p r.nonblock(false) { |io| [io.equal?(r), io.nonblock?] }, r.nonblock?
p r.nonblock { |io| io.nonblock? }
r.nonblock = false
p r.nonblock { |io| io.nonblock? }, r.nonblock?
begin
  r.nonblock
rescue LocalJumpError => e
  puts e.class
end
w.close
r.close

require "strscan"
p StringScanner::Version.class, StringScanner::Version < "1.0.0", StringScanner::Id.class
