# Process.getrlimit and Process.setrlimit, with the RLIMIT constants: the
# soft and the hard limit of a resource, named by a constant, a Symbol or
# a String.
p Process::RLIMIT_NOFILE.class, Process::RLIMIT_CORE.class, Process::RLIMIT_CPU.class, Process::RLIM_INFINITY.class
p %w[AS CORE CPU DATA FSIZE NOFILE NPROC STACK].all? { |r| Process.const_defined?("RLIMIT_#{r}") }
soft, hard = Process.getrlimit(:NOFILE)
p soft.class, hard.class, soft <= hard
p Process.getrlimit("NOFILE") == [soft, hard], Process.getrlimit(Process::RLIMIT_NOFILE) == [soft, hard]
p Process.getrlimit(:CORE).size, Process.getrlimit(:STACK).size, Process.getrlimit(:CPU).size
p Process.setrlimit(:NOFILE, soft, hard), Process.getrlimit(:NOFILE) == [soft, hard]
# the soft limit may always come down, and go back up to the hard limit
lower = soft > 16 ? soft - 1 : soft
p Process.setrlimit(:NOFILE, lower), Process.getrlimit(:NOFILE) == [lower, lower]
Process.setrlimit(:NOFILE, soft, lower) rescue p $!.class
p Process.getrlimit(:NOFILE).first <= lower
begin
  Process.getrlimit(:NOPE)
rescue ArgumentError => e
  p e.message
end
begin
  Process.setrlimit(:NOPE, 1)
rescue ArgumentError => e
  p e.message
end
begin
  Process.getrlimit(nil)
rescue TypeError => e
  p e.class
end
