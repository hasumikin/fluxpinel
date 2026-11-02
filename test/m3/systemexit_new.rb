# SystemExit.new(status, msg): the status and the message, as exit and
# abort set them, and the exit status of the program.

def show(e)
  p [e.class, e.status, e.success?, e.message]
end

show SystemExit.new
show SystemExit.new(1)
show SystemExit.new(true)
show SystemExit.new(false)
show SystemExit.new(3, "m")
show SystemExit.new(true, "bye")
show SystemExit.new(2.5)
show SystemExit.new("msg")
show SystemExit.new(nil)
show SystemExit.new(1, nil)

class MyExit < SystemExit; end
show MyExit.new(7)
begin
  raise MyExit.new(7)
rescue SystemExit => x
  show x
end

begin
  SystemExit.new("a", "b")
rescue ArgumentError => x
  puts x.message
end

begin
  exit 4
rescue SystemExit => x
  show x
end
begin
  exit false
rescue SystemExit => x
  show x
end
begin
  raise SystemExit, 6
rescue SystemExit => x
  show x
end

begin
  raise RuntimeError.new("r").status
rescue NoMethodError => x
  p x.receiver.message
end

raise SystemExit.new(5, "five")
