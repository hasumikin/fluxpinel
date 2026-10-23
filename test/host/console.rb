# AOT code of the kernel (test/kernel/app.rb) that drives a console session
# through Fluxpinel.session and Fluxpinel.feed. Only make test-host runs this
# file.

puts FxKernel.console(["x = 20", "def twice(n)", "  n * 2", "end", "twice(x) + 2", "_ / 2"])
# a piece sees the entries of the kernel and holds its objects
puts FxKernel.console(["c = FxKernel.make(\"ctr\"); nil","FxKernel.bump(c, 4)", "FxKernel.bump(c, 4)", "c.class"])
puts FxKernel.console(["FxKernel.boom(\"from the kernel\")", "raise IndexError, 'from a piece'", "1 2", ":still_here"])
# each session has locals of its own
puts FxKernel.console(["x"])
# an exit closes the session, not the program
puts FxKernel.console(["y = 1", "exit", "y + 1"])
puts :still_running
