# Writes the flags of the host build of an application of Roundhouse from
# its spin project:
#
#   ruby roundhouse/spin_flags.rb APP SPIN
#
# APP is the Spinel output of Roundhouse, which has a spin.toml. SPIN is the
# spin command of spinel. `spin flags` fetches the packages of the project,
# builds their C, and prints the flags of a build from outside spin. The
# script writes two files:
#
#   APP/.fx-kernel-flags   the flags of spinel for the kernel (-I, --rbs)
#   APP/.fx-host-libs      the objects and the libraries of the link
#
# The host runs the threads of the runtime, so the link takes the object
# that spin builds for a program with threads (NAME_mt.o) for each object of
# a package (NAME.o).
app, spin = ARGV
abort "usage: ruby #{$0} APP SPIN" unless spin
out = IO.popen([File.expand_path(spin), "flags"], chdir: app, err: File::NULL, &:read)
abort "spin_flags: `spin flags` failed in #{app}" unless $?.success?

kernel = []
libs = []
words = out.split
until words.empty?
  word = words.shift
  if word == "--link"
    item = words.shift or abort "spin_flags: --link with no value"
    if item.end_with?(".o")
      threads = item.delete_suffix(".o") + "_mt.o"
      abort "spin_flags: no #{threads} beside #{item}" unless File.exist?(threads)
      item = threads
    end
    libs << item
  else
    kernel << word
  end
end
File.write(File.join(app, ".fx-kernel-flags"), kernel.join(" ") + "\n")
File.write(File.join(app, ".fx-host-libs"), libs.join(" ") + "\n")
puts "spin_flags: #{kernel.size} words for the kernel, #{libs.size} for the link"
