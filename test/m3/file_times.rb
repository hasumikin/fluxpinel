# File.mtime, File.atime and File.ctime of a path: a Time from a stat, and
# Errno::ENOENT for no file.
t = File.mtime(__FILE__)
p t.class, t <= Time.now, File.atime(__FILE__).class, File.ctime(__FILE__).class
p File.mtime(__FILE__) == t, File.respond_to?(:mtime), File.respond_to?(:ctime)
begin
  File.mtime("/nonexistent/x")
rescue Errno::ENOENT => e
  p e.class
end
begin
  File.mtime(nil)
rescue TypeError => e
  p e.message
end
