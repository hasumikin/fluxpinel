# io/nonblock.rb -- IO#nonblock?, IO#nonblock= and IO#nonblock, in Ruby over
# IO#fcntl. spinel has no package of this name.

require "fcntl"

class IO
  # Whether the descriptor has O_NONBLOCK.
  def nonblock?
    (fcntl(Fcntl::F_GETFL) & Fcntl::O_NONBLOCK) != 0
  end

  # Sets O_NONBLOCK when mode is true, and clears it when mode is false.
  def nonblock=(mode)
    flags = fcntl(Fcntl::F_GETFL)
    wanted = mode ? flags | Fcntl::O_NONBLOCK : flags & ~Fcntl::O_NONBLOCK
    fcntl(Fcntl::F_SETFL, wanted) if wanted != flags
  end

  # Yields the IO in the mode, and puts the mode of before back after the
  # block. The answer is the value of the block.
  def nonblock(mode = true)
    before = nonblock?
    self.nonblock = mode
    begin
      yield self
    ensure
      self.nonblock = before
    end
  end
end
