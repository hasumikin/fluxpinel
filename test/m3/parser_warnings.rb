# The warnings of the parser go to Warning.warn when a file loads, as
# "path:line: warning: message": a program that defines Warning.warn gets
# them (a load and an eval), $VERBOSE true adds those of -w, $VERBOSE nil
# gives none, and a Warning.warn that raises ends the load.
$seen = []
module Warning
  def self.warn(msg, category: nil)
    $seen << msg.sub(/\A.*\//, "")
  end
end
file = File.join(__dir__, "requires/warned.rb")

load file
p $seen, WARNED_HASH
$seen.clear
eval("{ b: 1, b: 2 }")
p $seen.map { |m| m.sub(/\A.*\):/, "(eval):") }

$seen.clear
$VERBOSE = true
load file
# CRuby also warns of the constants and the method that the second load
# defines again: those are no warnings of the parser
p $seen.grep(/duplicated|= literal|not reached/)

$seen.clear
$VERBOSE = nil
load file
p $seen

$VERBOSE = false
module Warning
  def self.warn(msg, category: nil)
    raise ArgumentError, msg.strip.sub(/\A.*\//, "") if msg.include?("duplicated")
  end
end
begin
  load file
rescue ArgumentError => e
  puts e.message
end
