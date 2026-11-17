# File.identical? and File.fnmatch with its flags.
require "tmpdir"
root = Dir.mktmpdir("fx_fnmatch")
a = File.join(root, "a")
b = File.join(root, "b")
File.write(a, "x")
File.write(b, "x")
link = File.join(root, "l")
File.symlink(a, link)
p File.identical?(a, a), File.identical?(a, b), File.identical?(a, link)
p File.identical?(a, File.join(root, "none"))
File.delete(a, b, link)
Dir.rmdir(root)

p File.fnmatch("*.rb", "a.rb"), File.fnmatch("*.rb", "a.py"), File.fnmatch?("*", "a.rb")
p File.fnmatch("*.rb", ".a.rb"), File.fnmatch("*.rb", ".a.rb", File::FNM_DOTMATCH)
p File.fnmatch("*/a.rb", "x/y/a.rb"), File.fnmatch("*/a.rb", "x/y/a.rb", File::FNM_PATHNAME)
p File.fnmatch("**/a.rb", "x/y/a.rb", File::FNM_PATHNAME), File.fnmatch("**/a.rb", "a.rb", File::FNM_PATHNAME)
p File.fnmatch("A.RB", "a.rb"), File.fnmatch("A.RB", "a.rb", File::FNM_CASEFOLD)
p File.fnmatch("\\*", "*"), File.fnmatch("\\*", "*", File::FNM_NOESCAPE), File.fnmatch("\\*", "\\*", File::FNM_NOESCAPE)
p File.fnmatch("{a,b}.rb", "b.rb"), File.fnmatch("{a,b}.rb", "b.rb", File::FNM_EXTGLOB)
p File.fnmatch?("[a-c]?", "b1", File::FNM_PATHNAME | File::FNM_DOTMATCH), File.fnmatch("a*", "a/b", File::FNM_PATHNAME)
p((File.fnmatch("*", "a", "x") rescue $!.class))

# a wrong count of arguments
[-> { File.fnmatch("*") }, -> { File.fnmatch("*", "a", 0, 1) }, -> { File.identical?("a") }].each do |call|
  call.()
rescue ArgumentError => e
  puts e.message
end

# an open File stands for its file
File.open(__FILE__) do |f|
  p File.identical?(f, __FILE__), File.identical?(__FILE__, f), File.identical?(f, f), File.identical?(f, __dir__)
end
closed = File.open(__FILE__)
closed.close
p((File.identical?(closed, __FILE__) rescue $!.class))
