# The module functions of Process and GC, which spinel's codegen answers
# by name (process.c).

def t(label)
  r = yield
  puts "#{label}: #{r.inspect}"
rescue => e
  puts "#{label}: #{e.class} #{e.message}"
end

t("modules") { [Process, GC, Process.class, GC.class, Process::Status, Process::Tms] }
t("pid") { [Process.pid == $$, Process.pid.class, Process.ppid > 0, Process.getpgrp.class] }
t("ids") { [Process.uid.class, Process.gid.class, Process.euid == Process.uid, Process.egid == Process.gid] }
t("getsid") { [Process.getsid.class, Process.getsid(0) == Process.getsid] }
t("consts") { [Process::CLOCK_MONOTONIC, Process::CLOCK_REALTIME, Process::PRIO_PROCESS] }
t("clock") do
  a = Process.clock_gettime(Process::CLOCK_MONOTONIC)
  b = Process.clock_gettime(:CLOCK_MONOTONIC)
  [a.class, b >= a, Process.clock_gettime(Process::CLOCK_REALTIME, :second) > 1_000_000_000]
end
t("units") do
  %i[nanosecond microsecond millisecond second float_microsecond float_millisecond float_second].map do |u|
    Process.clock_gettime(Process::CLOCK_MONOTONIC, u).class
  end
end
t("bad unit") { Process.clock_gettime(Process::CLOCK_MONOTONIC, :hour) }
t("clock_getres") { Process.clock_getres(Process::CLOCK_MONOTONIC, :nanosecond).class }
t("times") do
  x = Process.times
  [x.class, x.utime.class, x.stime >= 0, x.cutime.class, x.cstime.class]
end
t("priority") { Process.getpriority(Process::PRIO_PROCESS, 0).class }
t("groups") { Process.groups.class }
t("GC.start") { [GC.start, GC.start(full_mark: true, immediate_sweep: false)] }
t("GC.stat") { [GC.stat.class, GC.total_time.class] }
t("respond_to?") { [Process.respond_to?(:pid), GC.respond_to?(:total_time), GC.respond_to?(:nope)] }
