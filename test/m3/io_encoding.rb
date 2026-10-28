# IO#external_encoding and #internal_encoding: set_encoding, binmode, and
# the default of a handle that reads or that only writes.

require "tmpdir"

p $stdin.external_encoding, $stdout.external_encoding, $stderr.external_encoding
p $stdin.internal_encoding, $stdout.internal_encoding
r, w = IO.pipe
p r.external_encoding, w.external_encoding
r.close
w.close

path = File.join(Dir.tmpdir, "fx_io_encoding_#{Process.pid}.txt")
File.write(path, "abc\n")
File.open(path) do |f|
  p f.external_encoding, f.internal_encoding
  p f.set_encoding("UTF-8").equal?(f), f.external_encoding
  f.set_encoding(Encoding::BINARY)
  p f.external_encoding
  f.binmode
  p f.external_encoding, f.binmode?
end
File.open(path, "w") { |f| p f.external_encoding }
File.open(path, "a") { |f| p f.external_encoding }
File.open(path, "r+") { |f| p f.external_encoding }
File.open(path, "w+") { |f| p f.external_encoding, f.set_encoding("UTF-8").external_encoding }
File.open(path, "rb") { |f| p f.external_encoding, f.read.encoding }
File.open(path, "r") { |f| p f.read.encoding }
File.delete(path)
