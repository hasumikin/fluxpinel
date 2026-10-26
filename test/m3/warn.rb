# Kernel#warn: one write to $stderr, each message on a line, and nothing
# when $VERBOSE is nil

class Sink
  attr_reader :text, :writes

  def initialize
    @text = +""
    @writes = 0
  end

  def write(*parts)
    @writes += 1
    parts.each { |x| @text << x.to_s }
    parts.sum { |x| x.to_s.size }
  end
end

sink = Sink.new
$stderr = sink
p warn("a", ["b", "c"], "d\n")
warn
warn "e", uplevel: nil
$VERBOSE = nil
warn "hidden"
$VERBOSE = false
warn 1, :sym
$stderr = STDERR
p sink.text, sink.writes
