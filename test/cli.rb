# cli.rb -- run the cases of test/cli/cases.rb: each one runs the command
# line of fluxpinel, in test/cli, with the arguments and the stdin of the
# case.
#
# Usage: ruby test/cli.rb FLUXPINEL
#
# A case with no :out runs the same arguments under ruby, and passes when
# stdout and the exit status are the same. With err: true, stderr must be
# the same too, after "ruby:" at the start of a line reads "fluxpinel:". A
# case with :out (a Regexp) passes when stdout matches it and the status
# is :status (0 by default).

require "open3"
require_relative "cli/cases"

fluxpinel = ARGV[0] or abort "usage: ruby test/cli.rb FLUXPINEL"
fluxpinel = File.expand_path(fluxpinel)
DIR = File.expand_path("cli", __dir__)

def run(cmd, args, stdin)
  Open3.capture3(cmd, *args, stdin_data: stdin || "", chdir: DIR)
end

failed = CLI_CASES.reject do |c|
  args = c[:args]
  out, err, st = run(fluxpinel, args, c[:stdin])
  why = nil
  if c[:out]
    want = c.fetch(:status, 0)
    why = "stdout #{out.inspect} does not match #{c[:out].inspect}" unless c[:out].match?(out)
    why ||= "status #{st.exitstatus}, not #{want}" unless st.exitstatus == want
  else
    rout, rerr, rst = run("ruby", args, c[:stdin])
    rerr = rerr.gsub(/^ruby:/, "fluxpinel:")
    why = "stdout: ruby #{rout.inspect}, fluxpinel #{out.inspect}" unless rout == out
    why ||= "status: ruby #{rst.exitstatus}, fluxpinel #{st.exitstatus}" unless rst.exitstatus == st.exitstatus
    why ||= "stderr: ruby #{rerr.inspect}, fluxpinel #{err.inspect}" if c[:err] && rerr != err
  end
  puts "#{why ? 'NG' : 'ok'} fluxpinel #{args.join(' ')}#{c[:stdin] ? ' < stdin' : ''}"
  puts "   #{why}" if why
  !why
end
puts "#{CLI_CASES.size - failed.size}/#{CLI_CASES.size} passed"
exit(failed.empty? ? 0 : 1)
