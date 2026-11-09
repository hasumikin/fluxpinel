# Dir handles: new, open with a block, for_fd, the readers and the
# position, Enumerable over the entries, dup, and Dir.chdir with a block.
require "tmpdir"
root = Dir.mktmpdir("fx_dir_handle")
File.write(File.join(root, "a.txt"), "a")
File.write(File.join(root, "b.txt"), "b")

d = Dir.new(root)
p d.class, d.path == root, d.to_path == root
p d.path.equal?(d.path), d.path.frozen?
p d.inspect == "#<Dir:#{root}>"
e = []
d.each { |n| e << n }
p e.sort
p d.read
d.rewind
first = d.read
p first.class
at = d.tell
second = d.read
p d.seek(at).equal?(d), d.read == second
p d.pos.is_a?(Integer), (d.pos = 0), d.read == first
p d.fileno.is_a?(Integer)
p d.children.sort, d.entries.sort
p d.each_child.to_a.sort, d.each.to_a.size
p d.count, d.include?("a.txt"), d.include?("zz"), d.include?(1)
p d.to_a.sort, d.select { |n| n.end_with?(".txt") }.sort, d.map(&:upcase).sort.last
p d.sort.first, d.reject { |n| n.start_with?(".") }.sort
p d.each_entry { |n| }.equal?(d)
p d.each_child { |n| }.equal?(d)
cs = []
d.each_child { |n| cs << n }
p cs.sort
p(d.each { |n| break n.size if n == "a.txt" })
# each starts from the first entry wherever a read left the stream
d.read
all = []
d.each { |n| all << n }
p all.sort

# dup: a new Dir with no stream and no path
c = d.dup
p c.class, c.path, c.equal?(d)
begin
  c.read
rescue IOError => err
  p err.class, err.message
end
p d.read.class

# for_fd: no path, the same entries over the same descriptor, so one close
w = Dir.for_fd(d.fileno)
p w.path, w.children.sort
p d.close
p d.path == root
begin
  d.read
rescue IOError => err
  p err.message
end
begin
  d.count
rescue IOError => err
  p err.message
end

# open with a block closes the handle, also on a raise
r = Dir.open(root) { |dd| dd.children.sort }
p r
h = nil
begin
  Dir.open(root) { |dd| h = dd; raise "boom" }
rescue RuntimeError => err
  p err.message
end
begin
  h.read
rescue IOError => err
  p err.message
end
p((Dir.new("/nonexistent_fx_dir") rescue $!.class))
p((Dir.new(root).no_such_method rescue $!.message.sub(root, "ROOT")))
p [Dir.new(root), 1][0] == nil, [Dir.new(root), 1][0] != nil

# Dir.chdir with a block: the path as the argument, back after the block,
# also on a raise, a break and a return
home = Dir.pwd
p Dir.chdir(root) { |path| [path == root, File.basename(Dir.pwd) == File.basename(root)] }
p Dir.pwd == home
begin
  Dir.chdir(root) { raise "inside" }
rescue RuntimeError => err
  p err.message
end
p Dir.pwd == home
p Dir.chdir(root) { break :brk }
p Dir.pwd == home
def ret(root) = Dir.chdir(root) { return :ret }
p ret(root), Dir.pwd == home
def cd(dir, &block) = Dir.chdir(dir, &block)
p cd(root) { |x| File.basename(x) == File.basename(root) }
p Dir.pwd == home
p Dir.chdir(root) { Dir.glob("*.txt", base: nil).sort }

File.delete(File.join(root, "a.txt"), File.join(root, "b.txt"))
Dir.rmdir(root)
