# The class methods of Dir: exist?, empty?, mkdir, children, entries,
# glob, each_child, foreach, rmdir.
root = File.join(Dir.pwd, "build", "dirtest_#{Process.pid}")
p Dir.exist?(root)
p Dir.mkdir(root)
p Dir.exist?(root), Dir.empty?(root), File.directory?(root)
File.write(File.join(root, "b.rb"), "")
File.write(File.join(root, "a.txt"), "")
Dir.mkdir(File.join(root, "sub"), 0o755)
File.write(File.join(root, "sub", "c.rb"), "")
p Dir.empty?(root), Dir.children(root).sort, Dir.entries(root).sort
p Dir.glob("*.rb", base: root), Dir.glob("**/*.rb", base: root).sort
p Dir.glob(["*.txt", "sub/*"], base: root).sort
p Dir.glob(File.join(root, "*.rb")).map { |f| File.basename(f) }
p Dir[File.join(root, "*")].map { |f| File.basename(f) }.sort
seen = []
p Dir.each_child(root) { |c| seen << c }
p seen.sort, Dir.each_child(root).to_a.sort
names = []
Dir.foreach(root) { |c| names << c }
p names.sort
p Dir.glob("*.rb", base: root) { |f| names << f }, names.size
File.delete(File.join(root, "sub", "c.rb"))
p Dir.rmdir(File.join(root, "sub"))
File.delete(File.join(root, "a.txt"), File.join(root, "b.rb"))
p Dir.rmdir(root), Dir.exist?(root)
begin
  Dir.children(root)
rescue SystemCallError => e
  p e.class
end
