# IO#write_nonblock: the count of bytes written now, :wait_writable (with
# exception: false) or IO::EAGAINWaitWritable when the pipe is full.
r, w = IO.pipe
p w.write_nonblock("abc")
p r.read(3)
p w.write_nonblock("é".b).class
chunk = "x" * 4096
total = 0
loop do
  n = w.write_nonblock(chunk, exception: false)
  break if n == :wait_writable
  total += n
end
p total > 0, total % 1 == 0
begin
  w.write_nonblock(chunk)
rescue IO::WaitWritable => e
  p e.class.ancestors.include?(IO::WaitWritable), e.class
end
p r.read(total).bytesize == total
p w.write_nonblock(:sym, exception: false), r.read(3)
begin
  r.write_nonblock("x")
rescue IOError => e
  puts e.message
end
w.close
begin
  w.write_nonblock("x")
rescue IOError => e
  puts e.message
end
r.close
