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

# the limit values: an Integer of the width of the type (a Bignum on
# Linux), the names INFINITY, SAVED_MAX and SAVED_CUR, a Float, and nil
# for the hard limit, which takes the soft one; the forms CRuby refuses
p Process::RLIM_SAVED_MAX == Process::RLIM_INFINITY, Process::RLIM_SAVED_CUR == Process::RLIM_INFINITY
fs_soft, fs_hard = Process.getrlimit(:FSIZE)
# where the infinity is a Bignum (Linux), so is the limit one below it
if Process::RLIM_INFINITY.bit_length > 63 && fs_hard == Process::RLIM_INFINITY
  Process.setrlimit(:FSIZE, Process::RLIM_INFINITY - 1)
  p Process.getrlimit(:FSIZE)[0] == Process::RLIM_INFINITY - 1
else
  p true
end
if fs_hard == Process::RLIM_INFINITY
  Process.setrlimit(:FSIZE, :INFINITY, "SAVED_MAX")
  p Process.getrlimit(:FSIZE) == [Process::RLIM_INFINITY, Process::RLIM_INFINITY]
  Process.setrlimit(:FSIZE, 2.0**20, :SAVED_CUR)
  p Process.getrlimit(:FSIZE) == [2**20, Process::RLIM_INFINITY]
  Process.setrlimit(:FSIZE, fs_soft, fs_hard)
else
  p true, true
end
c_soft, c_hard = Process.getrlimit(:CORE)
Process.setrlimit(:CORE, c_soft, nil)
p Process.getrlimit(:CORE) == [c_soft, c_soft]
# an Integer past the type is a RangeError; a build whose Integer cannot
# overflow (the host, in raise mode) has no such value, and says the same
big = (2**70 rescue nil)
[big, big && -big, big && -1 - big].each do |v|
  if v
    begin
      Process.setrlimit(:CORE, v)
    rescue RangeError => e
      p e.class
    end
  else
    p RangeError
  end
end
["RLIMIT_NOFILE", "nofile", "NO\0FILE", :RLIMIT_CORE].each do |n|
  Process.getrlimit(n)
rescue ArgumentError => e
  p e.message
end
["RLIM_INFINITY", "1", :infinity].each do |v|
  Process.setrlimit(:CORE, v)
rescue ArgumentError => e
  p e.message
end
# an Errno names the call
begin
  Process.getrlimit(99999)
rescue SystemCallError => e
  p e.class, e.message
end
