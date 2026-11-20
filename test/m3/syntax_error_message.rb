# The message of a SyntaxError, as CRuby writes it: the path and the line
# of the first error with "syntax error(s) found", then the lines of the
# source around the errors, each error under its line with a caret.
def show(src)
  eval(src, nil, "snippet.rb", 1)
  puts "no error"
rescue SyntaxError => e
  puts e.message, "--"
end
show("foo(1, 2\n")
show("p 1 2")
show("1 +")
show("x = 1\n)\ny = 2\n")
show("def a\n\nfoo(\n")
show("def m\n  if x\n    1\nend\n")
show("x = 1\ny = 2\nz = (3\n")
show("a = 1\n\n\n\n\nb = (\n\n\n\n\nc = 2\n")
show("def a\n" + "x = 1\n" * 9 + "foo(\n")
show(")\n" + "x = 1\n" * 8 + ")\n")
show("a = [1,\n2\n")
show("日本 = (1\n")

# a required file: its path leads the message
require "tmpdir"
Dir.mktmpdir do |dir|
  File.write(File.join(dir, "bad.rb"), "x = (1\nputs 2\n")
  begin
    require File.join(dir, "bad.rb")
  rescue SyntaxError => e
    real = Dir.chdir(dir) { Dir.pwd }
    puts e.message.gsub(real, "DIR").gsub(dir, "DIR")
  end
end
