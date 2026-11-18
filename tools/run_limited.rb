# run_limited.rb -- run a command with a limit of time, as timeout(1) of
# GNU coreutils does. macOS has no timeout(1), so the tools do not call it.
#
#   out, err, status = RunLimited.capture3(env, cmd, limit: SEC, chdir: DIR)
#
# status is the exit status of cmd, 128 + n when signal n ended it, and
# 124 when the limit passed. The command runs in a process group of its
# own. When the limit passes, the group gets TERM, and KILL two seconds
# after that.

require "open3"

module RunLimited
  TIMED_OUT = 124
  KILL_AFTER = 2

  def self.capture3(env, cmd, limit:, stdin_data: "", binmode: false, **opts)
    Open3.popen3(env, *cmd, pgroup: true, **opts) do |i, o, e, waiter|
      [i, o, e].each(&:binmode) if binmode
      out = Thread.new { o.read }
      err = Thread.new { e.read }
      feed = Thread.new do
        i.write(stdin_data)
      rescue Errno::EPIPE
        # the command closed its stdin first
      ensure
        i.close
      end
      late = !waiter.join(limit)
      if late
        signal_group("TERM", waiter.pid)
        unless waiter.join(KILL_AFTER)
          signal_group("KILL", waiter.pid)
          waiter.join
        end
      end
      feed.join
      st = waiter.value
      status = late ? TIMED_OUT : st.exitstatus || 128 + st.termsig.to_i
      [out.value, err.value, status]
    end
  end

  def self.signal_group(sig, pid)
    Process.kill(sig, -pid)
  rescue Errno::ESRCH, Errno::EPERM
    # the group ended already
  end
end
