# An instance of a class under String is a String: the methods of String
# run on it, and it keeps its class, its ivars and its own methods.

class Name < String
  attr_accessor :lang

  def initialize(s = "", lang = :en)
    super(s)
    @lang = lang
  end

  def shout = upcase + "!"
  def size = 99
end

n = Name.new("ruby", :ja)
p n, n.class, n.lang, n.is_a?(String), n.is_a?(Name), n.instance_of?(String), Name.ancestors.first(2)
p n.shout, n.size, n.length, n.bytesize, n.instance_variables

# a method that changes the String changes the instance; the others answer
# a plain String
n << "!"
p n, n.class, n.lang
n.upcase!
p n, n.class
p n.upcase.class, (n + "x").class, n.to_s.class, n.to_str.class, n[0, 2].class, (n * 2).class
p n.dup.class, n.clone.class, n.dup.lang, n.dup == n, n.dup.equal?(n)

# a String wherever one is wanted
p n == "RUBY!", "RUBY!" == n, n.eql?("RUBY!"), n.hash == "RUBY!".hash, "<#{n}>", "a" + n, [n, "x"].join("-")
h = { n => 1 }
p h["RUBY!"], h.key?(n), n =~ /UB/, n.start_with?("RU"), n.to_sym, File.basename(Name.new("/a/b.rb"))
puts n
$stdout.print n, "\n"
case n
when String then puts "a String"
end
case n
when Name then puts "a Name"
end

# the class goes with the value
def pass(v) = v
a = [pass(n)]
o = Struct.new(:v).new(n)
@held = n
p a[0].class, o.v.class, @held.class, a.first.lang, [n].map(&:class), { k: n }[:k].class, a.dup[0].equal?(n)

# no initialize: the arguments of String.new
class Plain < String; end
p Plain.new("abc"), Plain.new("abc").class, Plain.new, Plain.new.class, Plain.new("x").frozen?
p Plain.new("abc").inspect, Plain.new("abc").to_s.class, Plain.allocate.class

# freeze, clone and dup
f = Name.new("fz").freeze
p f.frozen?, f.clone.frozen?, f.dup.frozen?, f.clone.class, (f << "x" rescue $!.class)

# a class with no name, and a subclass of a subclass
k = Class.new(String) do
  def twice = self * 2
end
p k.new("ab").twice, k.new("ab").is_a?(String), k.superclass
class Deep < Name
  def initialize(s) = super(s, :de)
end
d = Deep.new("tief")
p d, d.class, d.lang, d.shout, Deep.superclass

# a singleton method and a comparison
def n.only = :one
p n.only, n <=> "RUBY!", [Name.new("b"), "a", Name.new("c")].sort, [Name.new("b"), "a"].min.class
