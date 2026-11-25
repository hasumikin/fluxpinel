# The frames of a file that a require or a load runs (backtrace_required.rb).
def trace_deep
  raise IndexError, "in the file"
end

puts caller.first(2).map { |l| l.sub(/\A.*\//, "") }
begin
  trace_deep
rescue IndexError => e
  puts e.backtrace.map { |l| l.sub(/\A.*\//, "") }
end
[1].each { puts caller(0, 1).map { |l| l.sub(/\A.*\//, "") } }
[1].each { [2].each { puts caller(0, 1).map { |l| l.sub(/\A.*\//, "") } } }
puts caller_locations(0, 1).map(&:base_label)
