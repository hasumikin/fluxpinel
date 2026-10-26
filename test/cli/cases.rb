# The cases of test/cli.rb. Each one runs in test/cli.

DESCRIPTION = /\Afluxpinel \d+ \(spinel \h+, ruby \d+\.\d+\.\d+\) \[[^\]]+\]\n/

CLI_CASES = [
  # -e, $0, __FILE__ and ARGV
  { args: ["-e", "p 1", "-e", "p 2"] },
  { args: ["-e", "p [$0, __FILE__, ARGV]", "a", "b"] },
  { args: ["-e", "exit 3"] },
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
  # the engine constants: the values of spinel, frozen
  { args: ["-e", "p RUBY_ENGINE, RUBY_VERSION, RUBY_PATCHLEVEL, RUBY_PLATFORM"],
    out: /\A"spinel"\n"\d+\.\d+\.\d+"\n0\n"\w+-\w[\w-]*"\n\z/ },
  { args: ["-e", "p [RUBY_VERSION, RUBY_ENGINE, RUBY_PLATFORM, RUBY_DESCRIPTION].all?(&:frozen?)"] },
  { args: ["-e", "puts RUBY_DESCRIPTION"], out: /#{DESCRIPTION}\z/ },
  { args: ["-e", "p RUBY_RELEASE_DATE, RUBY_REVISION.size, RUBY_COPYRIGHT.start_with?('ruby - Copyright')"],
    out: /\A"\d{4}-\d\d-\d\d"\n40\ntrue\n\z/ },
  # a wrong command line
  { args: ["nofile.rb"], err: true },
  { args: ["-Z"], err: true },
  { args: ["--nosuch"], err: true },
  { args: ["-e"], err: true },
  # the version and the help
  { args: ["-v"], out: /#{DESCRIPTION}\z/ },
  { args: ["--version"], out: /#{DESCRIPTION}\z/ },
  { args: ["-ve", "p $VERBOSE"], out: /#{DESCRIPTION}true\n\z/ },
  { args: ["-h"], out: /\AUsage: fluxpinel / },
  { args: ["--help"], out: /\AUsage: fluxpinel / },
].freeze
