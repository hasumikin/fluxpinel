# An exception that passes through an ensure clause keeps its message. The
# ensure clause here runs a handler of its own and allocates, so a
# collection runs while the exception waits. The report of the uncaught
# exception shows the message after that, so each case runs in a child and
# the parent reads its stderr.

def child_stderr
  r, w = IO.pipe
  pid = fork do
    r.close
    $stderr.reopen(w)
    yield
  end
  w.close
  err = r.read
  r.close
  Process.wait(pid)
  err
end

def held(path)
  entries = begin
    Dir.entries(path)
  rescue StandardError
    raise
  end
  entries.size
end

def through
  yield
ensure
  held(".")
end

# a raise of the walker by name, a raise of an object, and a raise of a
# message that the program builds
p child_stderr { through { 5.nope } }.include?("undefined method 'nope' for an instance of Integer")
p child_stderr { through { raise ArgumentError.new("an " + "object") } }.include?("an object (ArgumentError)")
p child_stderr { through { raise "built " + "message" } }.include?("built message (RuntimeError)")

# a rescue clause that does not take the exception evaluates its classes
# before the exception goes on
def classes = [ArgumentError, TypeError].map { |k| k }
def not_taken
  yield
rescue *classes
  :taken
end
p child_stderr { not_taken { 5.nope } }.include?("undefined method 'nope' for an instance of Integer")
