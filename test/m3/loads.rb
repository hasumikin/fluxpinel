# Kernel#load runs a file at each call and answers true, from the path as
# given (relative to the current directory, or absolute). A file that is
# not there raises LoadError.

path = File.join(__dir__, "requires", "counted.rb")
p load(path), $counted
p load(path), $counted, counted_helper
p load(path, false), $counted

begin
  load(File.join(__dir__, "requires", "nope.rb"))
rescue LoadError => e
  p e.class
end
begin
  load(nil)
rescue TypeError => e
  p e.message
end
