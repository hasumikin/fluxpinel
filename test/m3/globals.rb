# Global variables: the program's own and the special ones.

p $never_set
$count = 0
def bump(n = 1)
  $count += n
end
3.times { bump }
bump(10)
p $count

$log ||= []
$log << "a"
$log ||= ["lost"]
$log << "b"
p $log
$flag = true
$flag &&= "kept"
p $flag

class Registry
  def self.add(x)
    ($items ||= []) << x
  end
end
Registry.add(1)
Registry.add(:two)
p $items

p $VERBOSE, $DEBUG, $-v
$VERBOSE = nil
p $VERBOSE, $-w
p $/
p $stdout.class, $stderr.class, $stdin.class
p $stdout == STDOUT rescue p $stdout.equal?($stdout)
$stdout.puts "via $stdout", 1, [2, 3]
$stdout.print "a", "b", "\n"
p $stdout.write("w", 1, "\n")
$stdout << "x" << "y\n"
p $stdout.sync
$stdout.flush
p $LOAD_PATH.is_a?(Array)
p $PROGRAM_NAME == $0

class Sink
  attr_reader :parts
  def initialize
    @parts = []
  end

  def write(s)
    @parts << s
    s.size
  end

  def puts(*a)
    a.each { |x| write("#{x}\n") }
    nil
  end

  def print(*a)
    a.each { |x| write(x.to_s) }
    nil
  end
end

sink = Sink.new
orig = $stdout
$stdout = sink
puts "captured"
print "more"
p :sym
$stdout = orig
p sink.parts

begin
  $stdout = 42
rescue TypeError => e
  puts e.message
end
begin
  $? = 1
rescue NameError, SyntaxError => e
  puts e.class
end

# ENV
ENV["FX_T1"] = "one"
p ENV["FX_T1"], ENV.fetch("FX_T1"), ENV.key?("FX_T1")
p ENV["FX_NONE"], ENV.fetch("FX_NONE", "dflt"), ENV.fetch("FX_NONE") { |k| k.downcase }
begin
  ENV.fetch("FX_NONE")
rescue KeyError => e
  puts e.message
end
ENV["RAILS_ENV"] = "test" if ENV["RAILS_ENV"].nil?
p ENV["RAILS_ENV"]
p ENV.delete("FX_T1"), ENV.delete("FX_T1"), ENV.key?("FX_T1")
ENV.update("FX_T2" => "two", "FX_T3" => "three")
p ENV.select { |k, _| k.start_with?("FX_T") }.sort_by { |k, _| k }
ENV["FX_T2"] = nil
p ENV.key?("FX_T2"), ENV.to_h["FX_T3"], ENV.class, ENV.to_s
begin
  ENV["FX_T4"] = 4
rescue TypeError => e
  puts e.message
end

# ARGV: the arguments after the path, none here
p ARGV, ARGV.class
