# SignalException.new reads its signal: a name with or without the SIG, or
# a number. The message is the name with the SIG, and signo the number. A
# value that is no signal is an ArgumentError.

def t
  yield
rescue ArgumentError => e
  "#{e.class}: #{e.message}"
end

e = SignalException.new("TERM")
p e.message, e.signo, e.signm, e.class
p SignalException.new("SIGINT").message, SignalException.new("SIGINT").signo
p SignalException.new(15).message, SignalException.new(15).signo
p SignalException.new(15, "a message").message, SignalException.new(15, "a message").signo
p t { SignalException.new("bogus") }, t { SignalException.new }, t { SignalException.new("TERM", "x") }

# a subclass reads its signal the same way
class Stop < SignalException; end
p Stop.new("HUP").message, Stop.new("HUP").signo, t { Stop.new("nope") }

# raise makes the exception with new
begin
  raise SignalException, "USR1"
rescue SignalException => e
  p e.message, e.signo == Signal.list["USR1"]
end
p((raise SignalException, "bogus" rescue $!.class))

# Interrupt takes a message, and its signal is SIGINT
p Interrupt.new.message, Interrupt.new("stop").message, Interrupt.new.signo
