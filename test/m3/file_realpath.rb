# File.realpath and File.realdirpath: the path with the links and the dots
# resolved; realdirpath lets the last part be missing.
require "tmpdir"
Dir.mktmpdir do |d|
  Dir.chdir(d) do
    File.write("f", "")
    Dir.mkdir("sub")
    File.symlink("f", "lnk")
    File.symlink("sub", "slnk")
    real = File.realpath(d)
    p File.realpath("lnk") == File.join(real, "f")
    p File.realpath("slnk/../f") == File.join(real, "f")
    p File.realpath("f", d) == File.join(real, "f")
    p File.realpath("sub/..") == real
    p File.realpath(".") == real, File.realpath("./sub/") == File.join(real, "sub")
    p File.realdirpath("nope") == File.join(real, "nope")
    p File.realdirpath("slnk/nope") == File.join(real, "sub", "nope")
    p File.realdirpath("nope", d) == File.join(real, "nope")
    p File.realpath("f").frozen?, File.realpath("f").encoding
    begin
      File.realpath("nope")
    rescue SystemCallError => e
      p e.class
    end
    begin
      File.realdirpath("nope/x")
    rescue SystemCallError => e
      p e.class
    end
    begin
      File.realpath(1)
    rescue TypeError => e
      p e.class
    end
  end
end
