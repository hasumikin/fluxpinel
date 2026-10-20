# require of a library: the packages of spinel, their native_func
# bindings, Set with no require, and alias.

s = Set[3, 1, 2]
p s, s.size, s.include?(2), s == Set[1, 2, 3]
p s.map { |x| x * 10 }
p require("set")

p require("json")
p require("json")
p JSON.generate({ "a" => [1, 2.5, nil, true], "b" => "x" })
p JSON.parse('{"x": [1, {"y": "z"}], "n": null}')
p JSON.parse('{"x": 1}', symbolize_names: true)
begin
  JSON.parse(nil)
rescue TypeError => e
  puts e.message
end

require "base64"
p Base64.encode64("hello, world")
p Base64.strict_decode64("aGVsbG8=")

require "digest"
p Digest::SHA256.hexdigest("abc")
p Digest::MD5.digest("abc").bytesize

require "tmpdir"
p Dir.tmpdir.empty?

p require("thread"), require("fiber")
begin
  require "no_such_library_here"
rescue LoadError => e
  puts e.message
end

class Greeter
  def hello = "hello"
  alias hi hello
  alias_method :hey, :hello
  class << self
    def make = new
    alias build make
  end
end
p Greeter.build.hi, Greeter.new.hey
p [1, [2, [3]], "a", :b, nil].join("-")
p [1, 2].to_a, 1.eql?(1), 1.eql?(1.0)

# native classes: StringIO and StringScanner
require "stringio"
io = StringIO.new
io.puts "hello"
io.print 1, :a
io << "x" << 2
io.write("w", 3)
p io.string, io.pos
r = StringIO.new("line1\nline2\n")
p r.gets, r.read, r.eof?
p StringIO.new("abc").read(2)
p io.class, io.is_a?(StringIO), io.respond_to?(:puts)
p IO::WaitReadable.class
require "strscan"
sc = StringScanner.new("3 + 42")
p sc.scan(/\d+/), sc.scan(/\s*\+\s*/), sc.scan(/\d+/), sc.eos?
sc = StringScanner.new("abc")
p sc.scan("ab"), sc.pos, sc.rest
