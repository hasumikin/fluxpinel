# Monitor: the reentrant lock of the runtime, and MonitorMixin from
# spinel's builtins (autoloaded).
m = Monitor.new
p m.class, m.mon_owned?, m.mon_locked?
m.synchronize { m.synchronize { p [:deep, m.mon_owned?] } }
p m.enter, m.mon_locked?, m.try_enter, m.exit, m.exit, m.mon_locked?
p m.synchronize { 21 * 2 }

cond = m.new_cond
p cond.class
done = false
t = Thread.new do
  m.synchronize do
    done = true
    cond.signal
  end
end
m.synchronize { cond.wait_until { done } }
t.join
p done

class Counter
  include MonitorMixin
  def initialize
    super
    @n = 0
  end
  def bump = synchronize { @n += 1 }
  attr_reader :n
end
c = Counter.new
4.times.map { Thread.new { 25.times { c.bump } } }.each(&:join)
p c.n, c.mon_owned?
