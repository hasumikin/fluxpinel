# Process.spawn with :close for a standard stream: the child's descriptor is
# closed, where nil or false leave it inherited; a descriptor past 2 takes
# :close too, in slot 10 of the runtime, beside a closed standard one. The
# child asks the shell to dup the descriptor, which fails on a closed one in
# every sh alike, and says so on the descriptor named (a probe of 2 lets the
# message go to 2, closed when the dup fails, or the redirection would open
# it).
$stdout.sync = true
probe = ->(fd, to) { "(exec 3<&#{fd}) #{fd == 2 ? "" : "2>/dev/null "}&& echo '#{fd}: open' >&#{to} || echo '#{fd}: closed' >&#{to}" }
def capture
  r, w = IO.pipe
  yield w
  w.close
  print r.read
  r.close
end
capture { |w| Process.wait(spawn('sh', '-c', "#{probe[0, 1]}; #{probe[2, 1]}", in: :close, out: w)) }
capture { |w| Process.wait(spawn('sh', '-c', "#{probe[0, 2]}; #{probe[1, 2]}", out: :close, err: w)) }
capture { |w| Process.wait(spawn('sh', '-c', "#{probe[1, 1]}; #{probe[2, 1]}", err: :close, out: w)) }
capture { |w| system('sh', '-c', probe[0, 1], in: :close, out: w) }
p $?.success?
capture { |w| Process.wait(spawn('sh', '-c', "#{probe[1, 4]}; #{probe[2, 4]}", [:out, :err] => :close, 4 => w)) }
capture { |w| Process.wait(spawn('sh', '-c', "#{probe[0, 4]}; #{probe[5, 4]}", in: :close, 5 => :close, 4 => w)) }
Process.wait(spawn('sh', '-c', "echo 'err: the parent stdout' >&2; #{probe[1, 2]}", out: :close, err: 1))
pid = fork { exec('sh', '-c', probe[0, 1], in: :close) }
Process.wait(pid)
