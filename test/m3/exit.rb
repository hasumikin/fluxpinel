# Kernel#exit and #abort raise a SystemExit; one that no rescue takes ends
# the program with its status.

begin
  exit 3
rescue SystemExit => e
  p [e.class, e.status, e.success?, e.message]
end

begin
  exit
rescue SystemExit => e
  p [e.status, e.success?]
end

begin
  abort "to stderr"
rescue SystemExit => e
  p [e.status, e.message]
end

def leave(ok) = exit(ok)

begin
  leave(false)
rescue SystemExit => e
  p e.status
end

begin
  exit 1, 2
rescue ArgumentError => e
  p e.class
end

puts "end"
exit 4
