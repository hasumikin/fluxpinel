# Class.new with an exception class as the superclass

def mask(s) = s.to_s.gsub(/0x\h+/, "0xX")

k = Class.new(StandardError)
begin
  raise k, "boom"
rescue k => e
  p e.message, e.class == k, e.is_a?(StandardError)
  puts mask(e.inspect), mask(e.class)
end
begin
  raise k
rescue => e
  puts mask(e.message)
end
p k.superclass, k.ancestors.include?(StandardError), k.name

a = Class.new(ArgumentError) do
  def initialize(msg = "custom") = super("[#{msg}]")
  def extra = :extra
end
begin
  raise a
rescue ArgumentError => e
  p e.message, e.extra
end
begin
  raise a, "given"
rescue a => e
  p e.message
end

Named = Class.new(StandardError)
begin
  raise Named, "named"
rescue Named => e
  p e, e.class, e.class.name
end

class Parser
  Error = Class.new(ArgumentError)
  def self.parse(s) = s =~ /\A\d+\z/ ? s.to_i : raise(Error, "invalid: #{s}")
end
p Parser.parse("12")
begin
  Parser.parse("x")
rescue Parser::Error => e
  p e.class, e.message
end

begin
  Class.new(Comparable)
rescue TypeError => e
  p e.message
end

# detailed_message: no class for an unnamed class
p mask(k.new("m").detailed_message), mask(k.new.detailed_message)
p Named.new("q").detailed_message, Named.new("a\nb").detailed_message
p RuntimeError.new("").detailed_message, RuntimeError.new("x").detailed_message
class Own < StandardError
  def message = "own"
end
class Shown < StandardError
  def to_s = "shown"
end
p Own.new("stored").detailed_message, Shown.new("stored"), [Shown.new("stored")], Shown.new.message
begin
  raise +""
rescue => e
  p e.message, e.detailed_message
end
p ArgumentError.new("a\0b").message.bytes.size, ArgumentError.new("a\0b").detailed_message.bytes.size

# method_defined? of a builtin method answers as CRuby
p Time.method_defined?(:xmlschema), Time.method_defined?(:no_such)
p Time.method_defined?(:inspect), Time.method_defined?(:inspect, false)
p Comparable.method_defined?(:between?), Time.public_method_defined?(:year)
p k.method_defined?(:message), k.method_defined?(:no_such), a.method_defined?(:extra)
class Version
  include Comparable
  def <=>(o) = 0
end
p Version.method_defined?(:clamp), Version.method_defined?(:no_such)
class Time
  def stamp = :own unless method_defined?(:stamp)
  def year = :never unless method_defined?(:year)
end
p Time.at(0).stamp, Time.at(0).utc.year
