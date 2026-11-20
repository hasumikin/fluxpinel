# Kernel#caller_locations: the frames out of the call as
# Thread::Backtrace::Location objects, each with the path, the line, the
# label and the base label of its line of Kernel#caller.
def show(loc)
  p [loc.class, loc.path, loc.lineno, loc.label, loc.base_label, loc.to_s, loc.inspect,
     loc.absolute_path == File.expand_path(loc.path)]
end
def lab = caller_locations(1, 1).first.then { |l| [l.label, l.base_label] }

class Shop
  def self.open = new.serve
  def serve = twice { show caller_locations(0, 1).first }
  def twice
    yield
  end
  def m = lab
  def blk = [1].map { lab }.first
  def two = [1].map { [2].map { lab }.first }.first
  p lab
  class << self
    p lab
    def s = lab
  end
end
Shop.open
p Shop.s, Shop.new.m, Shop.new.blk, Shop.new.two
p proc { lab }.call, lambda { lab }.call

# start and length, as caller takes them
def once
  yield
end
def deep
  once do
    once do
      puts caller_locations(0, 3).map(&:to_s)
      p caller_locations(0, 3).map(&:lineno)
      p caller_locations(0, 3).map(&:to_s) == caller(0, 3)
      p caller_locations(2).size == caller(2).size
    end
  end
end
deep
p caller_locations(1, 1), caller_locations(100), caller_locations(0, 0), caller_locations.class
begin
  caller_locations(-1)
rescue ArgumentError => e
  p e.message
end
begin
  caller_locations(0, -1)
rescue ArgumentError => e
  p e.message
end
p caller_locations(0).first.respond_to?(:lineno), caller_locations(0).first.frozen?
