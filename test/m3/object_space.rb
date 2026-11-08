# ObjectSpace and ThreadGroup, the builtins/*.rb of spinel that Fluxpinel
# autoloads. A finalizer runs after its object is collected, at a loop's
# back-edge or a method's entry (not only at exit), and GC.start runs the
# finalizers of what it freed.
$fin = 0
class Res
  def initialize(n)
    @n = n
    @pad = "p" * 64
    ObjectSpace.define_finalizer(self, Res.fin)
  end
  def self.fin = proc { |_id| $fin += 1 }
end
i = 0
while i < 50_000
  Res.new(i)
  i += 1
end
p $fin > 0

keep = Res.new(-1)
gone = Res.new(-2)
ObjectSpace.undefine_finalizer(gone)
begin
  ObjectSpace.define_finalizer(1, proc { |_id| })
rescue ArgumentError => e
  puts e.message
end
p ObjectSpace.define_finalizer(keep, proc { |_id| })[0]

p ThreadGroup::Default.class
tg = ThreadGroup.new
t = Thread.new { sleep 0.05 }
tg.add(t)
p tg.list.size
t.join
p tg.list.size, tg.enclosed?
