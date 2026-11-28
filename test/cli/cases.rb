# The cases of test/cli.rb. Each one runs in test/cli.

require "tmpdir"

DESCRIPTION = /\Afluxpinel \d+ \(spinel \h+, ruby \d+\.\d+\.\d+\) \[[^\]]+\]\n/

CLI_CASES = [
  # -e, $0, __FILE__ and ARGV
  { args: ["-e", "p 1", "-e", "p 2"] },
  { args: ["-e", "p [$0, __FILE__, ARGV]", "a", "b"] },
  { args: ["-e", "exit 3"] },
  # a NameError in -e has no mark under its source, as in ruby (test/m3/name_error_mark.rb has the file)
  { args: ["-e", "begin; nil.nosuch_zzz; rescue => e; puts e.detailed_message(highlight: false); end"] },
  # the file of the program, and the arguments after it
  { args: ["args.rb", "a", "-e", "b"] },
  { args: ["--", "args.rb", "-x"] },
  # stdin
  { args: [], stdin: "p [$0, __FILE__, ARGV]\n" },
  { args: ["-", "a", "b"], stdin: "p [$0, __FILE__, ARGV]\n" },
  # $VERBOSE
  { args: ["-W0", "-e", "p $VERBOSE"] },
  { args: ["-W1", "-e", "p $VERBOSE"] },
  { args: ["-W", "-e", "p $VERBOSE"] },
  { args: ["-w", "-e", "p $VERBOSE"] },
  { args: ["-we", "p $VERBOSE"] },
  # -c
  { args: ["-c", "args.rb"] },
  { args: ["-c", "bad.rb"] },
  # -I and -r
  { args: ["-I", "lib", "-e", "p $LOAD_PATH.first == File.expand_path('lib')"] },
  { args: ["-Ilib", "-Iother", "-e", "p $LOAD_PATH.first(2).map { |d| File.basename(d) }"] },
  { args: ["-r", "./lib/greet.rb", "-e", "p Greet.hi"] },
  { args: ["-r./lib/greet", "args.rb", "x"] },
  # a library of $LOAD_PATH, by its name
  { args: ["-Ilib", "-rgreet", "-e", "p Greet.hi"] },
  { args: ["-I", "lib", "-e", "p require('greet'), require('greet.rb'), Greet.hi"] },
  { args: ["-e", "$LOAD_PATH.unshift(File.expand_path('lib')); p require('greet'), Greet.hi"] },
  { args: ["-e", "begin; require 'greet'; rescue LoadError => e; p e.message; end"] },
  # RUBYLIB, after -I
  { args: ["-Iother", "-e", "p $LOAD_PATH.first(3).map { |d| File.basename(d) }"],
    env: { "RUBYLIB" => "lib:#{File.expand_path('lib', __dir__)}" } },
  { args: ["-e", "p $LOAD_PATH.first == File.expand_path('lib')"], env: { "RUBYLIB" => "lib" } },
  # the gems of the build (build/gem_dirs.h) come last in $LOAD_PATH, for
  # every program and not only --irb: a program requires reline with no
  # RUBYLIB, and a copy of -I or RUBYLIB comes first
  { args: ["-e", "require 'reline'; puts Reline.name"], env: { "RUBYLIB" => nil }, out: /\AReline\n\z/ },
  { args: ["-Ifake_reline", "-e", "require 'reline'; p Reline::FAKE"], env: { "RUBYLIB" => nil } },
  { args: ["-e", "require 'reline'; p Reline::FAKE"], env: { "RUBYLIB" => "fake_reline" } },
  # --irb: the IRB of CRuby on stdin; the arguments after it are its
  # options, and HOME keeps the history out of the home of the user
  { args: ["--irb", "-f", "--nocolorize", "--noprompt"], stdin: "1 + 1\n[$0, ARGV]\nrequire 'greet'\nGreet.hi\n",
    env: { "HOME" => Dir.tmpdir, "RUBYLIB" => "lib" }, out: /^2\n.*^\["irb", \[\]\]\n.*^true\n.*^"hi"\n+\z/m },
  { args: ["-rgreet", "-Ilib", "--irb", "-f", "--nocolorize", "--noprompt"], stdin: "Greet.hi\n",
    env: { "HOME" => Dir.tmpdir }, out: /^"hi"\n+\z/ },
  # the ri driver of the completion dialog: rdoc 8.1 requires rbs there,
  # which does not load, and the input method answers nil for the driver
  { args: ["--irb", "-f", "--nocolorize", "--noprompt"],
    stdin: "IRB::RelineInputMethod.new(IRB::RegexpCompletor.new).rdoc_ri_driver.nil?\n",
    env: { "HOME" => Dir.tmpdir }, out: /^true\n+\z/ },
  # a syntax and a call that Fluxpinel does not have raise in a session,
  # and stop a program
  { args: ["--irb", "-f", "--nocolorize", "--noprompt"], stdin: "undef :\"a\#{1}\"\n1.succ { 2 }\n1 + 1\n",
    env: { "HOME" => Dir.tmpdir },
    out: /unsupported undef of an interpolated name \(PM_UNDEF_NODE\) \(NotImplementedError\).*unsupported block for the builtin Integer#succ \(NotImplementedError\).*^2\n+\z/m },
  { args: ["-e", "1.succ { 2 }"], out: /\A\z/, status: 2 },
  # a redirection of a descriptor past 2 to a descriptor of the child
  # ([:child, fd]) is a form Fluxpinel does not make: a gap, not the
  # TypeError of a path
  # a descriptor of the child that no option redirects is CRuby's ArgumentError (test/m3/spawn_child_redirect.rb has the forms that work)
  { args: ["-e", "begin; exec('true', 3 => [:child, :out]); rescue ArgumentError => e; puts e.message; end"] },
  # spawn and system take a descriptor past 2 too (fx_spawn_fork), as exec does
  { args: ["-e", "system('sh', '-c', 'echo three >&3', 3 => $stdout); p $?.exitstatus"] },
  # an irb that parses with ripper: --irb names the gems and `make gems`
  { args: ["-Iold_irb", "--irb"], out: /\A\z/, status: 1,
    stderr: /\Afluxpinel: IRB does not load \(cannot load such file -- ripper\)\.\n.*irb \(1\.18 or later\).*`make gems`/m },
  # --irb-port: the program itself is the telnet client of its listener.
  # The case needs a Reline that runs on a socket (GEM_DIRS_FIRST of the
  # Makefile names its lib directory).
  { args: ["--irb-port", "0", "remote_irb.rb"], env: { "HOME" => Dir.tmpdir },
    out: /\A=> 2\n=> "abc"\n=> :twice\n=> 42\n=> 8\nabort then interrupt!\n\^C\nbye\nclosed: ""\n\z/, skip: /\Askip\n\z/ },
  { args: ["--irb-port", "x", "remote_irb.rb"], out: /\A\z/, status: 1 },
  # IO.console under a pseudo terminal, with the checks of the collector on
  # inside it: the open of /dev/tty, which a run with no terminal never
  # reaches
  { args: ["console_tty.rb"], env: { "FLUXPINEL" => File.expand_path(ARGV[0]) },
    out: /\A\[File, true, 2, true, "\/dev\/tty"\]\n2\n2\n2000\n50\nstatus 0\n\z/ },
  # a thread with its own $stdout and $stderr (the slots of src/global.c)
  { args: ["-rstringio", "-e", "o = StringIO.new; t = Thread.new { Thread.current[:fluxpinel_stdout] = o; Thread.current[:fluxpinel_stderr] = o; puts 1; $stdout.print 2; warn 3; $stderr << 4; p $stdout.equal?(o) }; t.join; puts 5; $stdout.puts 6; p o.string"],
    out: /\A5\n6\n"1\\n23\\n4true\\n"\n\z/ },
  # the engine constants: the values of spinel, frozen
  { args: ["-e", "p RUBY_ENGINE, RUBY_VERSION, RUBY_PATCHLEVEL, RUBY_PLATFORM"],
    out: /\A"spinel"\n"\d+\.\d+\.\d+"\n0\n"\w+-\w[\w-]*"\n\z/ },
  { args: ["-e", "p [RUBY_VERSION, RUBY_ENGINE, RUBY_PLATFORM, RUBY_DESCRIPTION].all?(&:frozen?)"] },
  { args: ["-e", "puts RUBY_DESCRIPTION"], out: /#{DESCRIPTION}\z/ },
  # RbConfig.ruby is this binary, by its real path
  { args: ["-e", "puts RbConfig.ruby"], out: /\A\/.*\/fluxpinel\n\z/ },
  { args: ["-e", "p RUBY_RELEASE_DATE, RUBY_REVISION.size, RUBY_COPYRIGHT.start_with?('ruby - Copyright')"],
    out: /\A"\d{4}-\d\d-\d\d"\n40\ntrue\n\z/ },
  # SIGINT and SIGTERM with no trap, which the standalone command raises
  # (the host build leaves the signals to its host)
  { args: ["signal_default.rb"] },
  # a wrong command line
  { args: ["nofile.rb"], err: true },
  { args: ["-Z"], err: true },
  { args: ["--nosuch"], err: true },
  { args: ["-e"], err: true },
  # --enable and --disable, as ruby takes them: =list, -name, or the next
  # word. gems: Gem is not defined, and a require of rubygems defines it.
  # frozen_string_literal: each plain literal of each file is frozen. The
  # other features of ruby (error_highlight, did_you_mean, syntax_suggest,
  # rubyopt, yjit, zjit) are accepted and change nothing: Fluxpinel has
  # none of them. A feature ruby does not know gets its two warnings.
  { args: ["--disable-gems", "-e", "p defined?(Gem); p require('rubygems'); p defined?(Gem), require('rubygems')"] },
  { args: ["--disable=gems,did_you_mean", "-e", "p defined?(Gem), defined?(RbConfig)"] },
  { args: ["--disable", "all", "-e", "p defined?(Gem)"] },
  { args: ["--enable=gems", "--disable=yjit,zjit,error_highlight,syntax_suggest,rubyopt", "-e", "p defined?(Gem)"] },
  { args: ["--enable=frozen-string-literal", "-e", "p 'a'.frozen?, \"a\#{1}\".frozen?, String.new('a').frozen?"] },
  { args: ["--enable-frozen_string_literal", "-rfrozen_lit", "-e", "p FROZEN_LIT.frozen?, 'b'.frozen?"] },
  { args: ["--disable=frozen_string_literal", "-e", "p 'a'.frozen?"] },
  { args: ["--disable=", "-e", "p 1"], err: true },
  { args: ["--enable", "-e", "p 1"], err: true },
  { args: ["--disable=foo", "-e", "p 1"], err: true },
  { args: ["--enable-foo", "-e", "p 1"], err: true },
  # with gems off, a require of rubygems loads rbconfig too, as the rubygems
  # of CRuby requires it, and RbConfig.ruby is this binary after either
  # require, as after the autoload
  { args: ["--disable-gems", "-e", "require 'rubygems'; p defined?(RbConfig), RbConfig::CONFIG.class, require('rbconfig')"] },
  { args: ["--disable-gems", "-e", "p require('rbconfig'), require('rubygems'), require('rbconfig'), require('rubygems')"] },
  { args: ["--disable-gems", "-e", "require 'rbconfig'; puts RbConfig.ruby"], out: /\A\/.*\/fluxpinel\n\z/ },
  { args: ["--disable-gems", "-e", "require 'rubygems'; puts RbConfig.ruby"], out: /\A\/.*\/fluxpinel\n\z/ },
  # the version and the help
  { args: ["-v"], out: /#{DESCRIPTION}\z/ },
  { args: ["--version"], out: /#{DESCRIPTION}\z/ },
  { args: ["-ve", "p $VERBOSE"], out: /#{DESCRIPTION}true\n\z/ },
  { args: ["-h"], out: /\AUsage: fluxpinel / },
  { args: ["--help"], out: /\AUsage: fluxpinel / },
].freeze
