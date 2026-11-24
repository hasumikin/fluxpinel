# pty.rb -- PTY.open and the block form of PTY.spawn, in Ruby over spinel's
# pty package.
#
# The package leaves them out for the typing of a compiled program: an
# answer that is an Array or nil would make every caller's reader and
# writer boxed values that lose the methods only a typed IO has. The walker
# has no such loss, so Fluxpinel adds them when the package loads
# (src/load.c), from the package's own master (Native.open_master, with
# its slave unlocked) and libc's ptsname.
#
# PTY.open answers [master, slave]: the master an IO in sync, the slave a
# File on its /dev path, opened without making it the controlling terminal
# (as CRuby opens it). With a block the two are yielded and closed after it,
# and the block's value is the answer. PTY.spawn with a block yields the
# reader, the writer and the pid and answers nil; the reader and the writer
# stay open and the child stays the caller's to wait for, as under CRuby
# 4.1 (whose block form no longer detaches the child).
module PTY
  module Native
    # the answer is libc's own buffer, which the next call writes again:
    # :cstring copies it
    native_func :ptsname, [:int], :cstring, "ptsname"
  end

  def self.open
    fd = Native.open_master
    path = Native.ptsname(fd)
    master = IO.new(fd, "r+")
    master.sync = true
    begin
      slave = File.open(path, File::RDWR | File::NOCTTY)
    rescue Exception
      master.close
      raise
    end
    return [master, slave] unless block_given?
    begin
      yield master, slave
    ensure
      master.close unless master.closed?
      slave.close unless slave.closed?
    end
  end

  class << self
    alias __fx_spawn_without_block spawn
    def spawn(*args, chdir: nil)
      reader, writer, pid = __fx_spawn_without_block(*args, chdir: chdir)
      return [reader, writer, pid] unless block_given?
      yield reader, writer, pid
      nil
    end
  end
end
