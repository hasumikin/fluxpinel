# Exception#full_message, detailed_message, clone and dup.
def boom = raise("bang")
begin
  boom
rescue => e
  print e.full_message(highlight: false, order: :top)
  print e.full_message(highlight: false, order: :bottom)
  p e.full_message(highlight: true)
  p e.detailed_message, e.detailed_message(highlight: true)
  c = e.clone
  p c.equal?(e), c.message, c.backtrace == e.backtrace, c.class
  c.set_backtrace(["x:1:in 'y'"])
  p e.backtrace.size > 1, c.backtrace
end
e2 = RuntimeError.new("a\nb")
e2.set_backtrace(["f:1:in q"])
p e2.full_message(highlight: false)
p e2.full_message(highlight: true)
p e2.detailed_message(highlight: true)
p RuntimeError.new("x").full_message(highlight: false)
p RuntimeError.new.detailed_message

class Tagged < StandardError
  def initialize(tag)
    @tag = tag
    super("tagged")
  end
  def message = "tag=#{@tag}"
end
begin
  raise Tagged.new(7)
rescue Tagged => e
  d = e.dup
  p d.message, d.instance_variable_get(:@tag), d.frozen?
  p e.freeze.clone.frozen?, e.dup.frozen?
  p e.detailed_message
end
