# Changes the Spinel output of Roundhouse for an application, for the
# console:
#
#   ruby roundhouse/apply.rb APP
#
# APP is the output directory of `roundhouse --target spinel`. The script
# changes APP/main.rb, writes console.rb with the models of the application
# and copies kernel/fluxpinel.rb into APP. It writes APP/.fx-ext-class, the
# classes of the `--ext-class` of the host build. Each change finds its
# anchor line in main.rb. When an anchor is not there exactly once, the
# script stops and changes nothing, so a change of the Roundhouse output
# cannot put a change in the wrong place.

FX = File.expand_path("..", __dir__)

blog = ARGV[0] or abort "usage: ruby #{$0} APP"
main = File.join(blog, "main.rb")
src = File.read(main)
abort "apply: #{main} has the changes already" if src.include?("Fluxpinel demo:")

lines = src.lines

# The models of the application: Roundhouse writes app/models/NAME_row.rb
# for each table, and app/models/NAME.rb has the class.
models = Dir.glob(File.join(blog, "app/models/*_row.rb")).sort.map do |row|
  file = row.delete_suffix("_row.rb") + ".rb"
  head = File.exist?(file) && File.foreach(file).find { |l| l.match?(/\Aclass \S+ < /) }
  abort "apply: #{file} has no line `class NAME < ...` (see ROUNDHOUSE_COMMIT)" unless head
  head[/\Aclass (\S+) < /, 1]
end
abort "apply: #{blog}/app/models has no model (no file *_row.rb)" if models.empty?

# console.rb, with a `when` for each model in place of each `# arms:` line.
console = File.readlines(File.join(__dir__, "console.rb"))
arms = console.grep(/\A\s*# arms: /).size
abort "apply: console.rb has #{arms} lines `# arms:`, not 3" unless arms == 3
console = console.flat_map do |line|
  arm = line.match(/\A(\s*)# arms: (.*)\n\z/) or next [line]
  models.map { |name| "#{arm[1]}when \"#{name}\" then #{arm[2].sub("MODEL", name)}\n" }
end

# The index of the line that is text. Stops when not exactly one line is.
def anchor(lines, text)
  found = lines.each_index.select { |i| lines[i].chomp == text }
  return found[0] if found.size == 1
  abort "apply: main.rb has #{found.size} lines `#{text}`, not 1 (see ROUNDHOUSE_COMMIT)"
end

boot = anchor(lines, 'require_relative "boot"')
start = anchor(lines, 'port = (ENV["PORT"] || "3000").to_i')
name = anchor(lines, "Tep::APP.name = Rails.application.global_id_app")
abort "apply: the last line of main.rb is not `end`" unless lines.last.chomp == "end"
abort "apply: the anchors of main.rb are not in order" unless boot < start && start < name

# The server starts in an entry, Main.start, so the host build calls it
# after the top level of the kernel. Insert from the bottom up, so each
# index stays right.
lines.push("nil\n", "end\n", "end\n")
lines.insert(name + 1, <<~RUBY)
  console_port = ENV["FLUXPINEL_CONSOLE"].to_s.to_i
  if console_port > 0
    Thread.new do
      Console.listen(console_port)
    end
  end
RUBY
lines.insert(start, <<~RUBY)
  #
  # Fluxpinel demo: the server starts in an entry, Main.start, so that a
  # host build can run Fluxpinel beside it. FLUXPINEL_CONSOLE=PORT opens
  # the console on 127.0.0.1:PORT.
  module Main
  #: (Array[String]) -> nil
  def self.start(argv)
RUBY
lines.insert(boot + 1, "require_relative \"console\"\n")

File.write(main, lines.join)
File.write(File.join(blog, "console.rb"), console.join)
File.write(File.join(blog, "fluxpinel.rb"), File.read(File.join(FX, "kernel/fluxpinel.rb")))
# The classes whose methods IRB calls by name through the table of
# --ext-class: the models, and the classes of Active Record whose objects
# their methods answer (a Relation of Article.all).
File.write(File.join(blog, ".fx-ext-class"), (models + %w[ActiveRecord::Base ActiveRecord::Relation]).join(",") + "\n")
puts "apply: changed #{main} (models: #{models.join(", ")})"
