# The class methods of File on paths (file.c), on files in /tmp that the
# test makes and deletes.

def t(label)
  r = yield
  puts "#{label}: #{r.inspect}"
rescue => e
  puts "#{label}: #{e.class} #{e.message}"
end

class Where
  def initialize(path) = @path = path
  def to_path = @path
end

class Named
  def to_str = "named"
end

dir = "/tmp"
a = File.join(dir, "lupe_files_a.txt")
t("join") { [File.join("a", "b"), File.join("a/", "/b"), File.join("a", ["b", ["c"]]), File.join, File.join("x")] }
t("join to_path") { File.join(Where.new("w"), Named.new) }
t("join nil") { File.join("a", nil) }
t("write") { [File.write(a, "hello\n"), File.read(a), File.size(a)] }
t("write to_s") { [File.write(a, 42), File.read(a)] }
t("write offset") { [File.write(a, "HELLO", 1), File.read(a)] }
t("write nil offset") { [File.write(a, "abc", nil), File.read(a)] }
t("write mode") { [File.write(a, "def", mode: "a"), File.read(a)] }
t("write perm") { [File.write(a, "x", perm: 0o600), File.read(a)] }
t("binwrite") { [File.binwrite(a, "\x00\x01"), File.binread(a).bytes, File.binread(a).encoding] }
t("read") { File.write(a, "0123456789"); [File.read(a, 3), File.read(a, 3, 5), File.read(a, nil, 8), File.read(a).encoding] }
t("read to_path") { File.read(Where.new(a)) }
t("read missing") { File.read(File.join(dir, "lupe_files_none")) }
t("predicates") do
  [File.exist?(a), File.file?(a), File.directory?(a), File.directory?(dir), File.file?(dir),
   File.exist?(dir + "/lupe_files_z")]
end
t("names") do
  [File.basename(a), File.basename(a, ".txt"), File.basename(a, ".*"), File.dirname("/x/y/z"), File.extname(a),
   File.extname("x")]
end
t("expand") { [File.expand_path("b", "/a"), File.expand_path("../c", "/a/b"), File.absolute_path("/q/./r")] }
t("respond_to?") { [File.respond_to?(:join), File.respond_to?(:write), File.respond_to?(:nope)] }
b = File.join(dir, "lupe_files_b.txt")
File.write(b, "")
t("delete") { [File.delete(a, b), File.exist?(a), File.exist?(b)] }
t("delete missing") { File.unlink(a) }
t("delete nil") { File.delete(nil) }
