# IO.new, IO.for_fd and IO.open with no mode take the mode of the descriptor
# (its access mode and O_APPEND, as CRuby reads them with fcntl): a
# write-only descriptor writes and a read-only one reads, each refusing the
# other way; the stream of an appending descriptor appends. The keywords of
# the encoding set it, as set_encoding does.
require "tmpdir"
r, w = IO.pipe
wi = IO.new(w.fileno)
ri = IO.for_fd(r.fileno)
wi.puts "through the pipe"
wi.flush
p ri.gets
begin
  ri.puts "x"
rescue IOError => e
  p e.message
end
begin
  wi.read
rescue IOError => e
  p e.message
end
path = File.join(Dir.tmpdir, "io_new_mode_#{Process.pid}")
File.write(path, "first\n")
f = File.open(path, "a")
ai = IO.open(f.fileno)
ai.write("appended\n")
ai.flush
p File.read(path)
f.close
f = File.open(path, "r+")
rw = IO.new(f.fileno)
rw.write("FIRST")
rw.flush
rw.rewind
p rw.read
f.close
File.delete(path)
# the keywords of the encoding, as set_encoding takes them; a writable
# handle with none has no external encoding, as in CRuby
r, w = IO.pipe
k = { autoclose: false }
p IO.new(r.fileno, **k).external_encoding, IO.new(w.fileno, **k).external_encoding
p IO.new(w.fileno, external_encoding: "UTF-8", **k).external_encoding
p IO.new(w.fileno, encoding: "ASCII-8BIT", **k).external_encoding
p IO.open(w.fileno, external_encoding: Encoding::UTF_8, internal_encoding: "-", **k).external_encoding
p IO.new(w.fileno, binmode: true, **k).external_encoding
# the standard output of a program under a pipe is write-only
o = IO.new(1)
$stdout.flush
o.puts "via descriptor 1"
o.flush
