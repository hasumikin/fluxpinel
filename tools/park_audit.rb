# park_audit.rb -- find a call of the walker into a wait of the runtime that
# keeps the walker lock.
#
# Usage: ruby park_audit.rb SPINEL FILE...
#
# SPINEL is the spinel checkout. FILE... are the C files of Fluxpinel (src/*.c
# and the generated wrappers). The script:
#
#   1. reads the functions of the runtime (lib/*.c, lib/*.h) and finds the
#      ones that reach a park of the green thread: sp_sched_block and its
#      kin, the I/O wait, a leave of the world for a native call,
#   2. finds each call of such a function in FILE... that is not inside
#      FX_UNLOCKED(...), and not in a function that runs without the lock
#      (ALLOW below),
#   3. prints the calls, and exits 1 when there is one.
#
# A wait inside a collection is not a park here: a collection ends without
# the walker lock, so a thread that waits for one with the lock held stops
# nobody. The functions of the collector and the allocator do not pass a
# park on (STOP below).
#
# PARK_AUDIT_TABLE=FILE writes the runtime functions that reach a park.

require "set"

SPINEL, *FILES = ARGV
abort "usage: ruby park_audit.rb SPINEL FILE..." if FILES.empty?

# The primitives that park the green thread or leave the world.
SINKS = %w[sp_sched_block sp_sched_block_wl sp_sched_block_timeout
           sp_sched_wait_io_impl sp_native_enter sp_stw_park_locked
           sp_sched_switch_out]

# Functions whose park does not pass on to their callers, with the reason.
STOP = {
  /\Asp_gc_/ => "a collection ends without the walker lock",
  /\Asp_alloc/ => "an allocation parks only for a collection",
  /\Asp_slab_/ => "the slab allocator parks only for a collection",
  /\Asp_stw_collect/ => "a collection ends without the walker lock",
  /\Asp_cext_/ => "the C extension lock is taken inside a collection",
  /\Asp_sched_(worker|pump|pass_locked|drain|conc_|par_|stw_)/ => "the scheduler itself",
  /\Asp_(cs_|out_)/ => "the chunk server and the idle accounting of the scheduler",
  /\Asp_fiber_(fire_inject|defer_inject|resume|switch|yield|transfer)/ => "a fiber switch is not a wait",
  /\Asp_Thread_(new|start|spawn|main|current|kill|raise|wakeup)\z/ => "a thread call that does not wait",
  /\Asp_exit\z|\Asp_at_exit|\Asp_main_end/ => "the end of the program",
}

# Calls that keep the lock on purpose: "function" for every call of the
# function, "function callee" for one call. A poly helper parks for one
# receiver (a Queue, an IO, a Thread), which the guard of the function or
# an earlier dispatch keeps away from it.
ALLOW = {
  "fx_lock" => "the walker lock itself",
  "fx_unlock" => "the walker lock itself",
  "fx_lock_set_depth" => "the walker lock itself",
  "fx_array_core sp_poly_pop" => "the receiver is an Array; a Queue takes fx_fiber_core",
  "fx_array_core sp_poly_shift" => "the receiver is an Array; a Queue takes fx_fiber_core",
  "fx_array_core sp_poly_pop_any" => "the receiver is an Array; a Queue takes fx_fiber_core",
  "fx_builtin_call sp_poly_join" => "the receiver is an Array; a Thread takes its wrapper",
  "fx_string_core sp_poly_shl" => "the receiver is a String; an IO takes fx_io_core",
  "fx_str_joined sp_poly_shl" => "the receiver is a new String",
  "fx_core sp_poly_shl" => "an IO takes fx_io_core before fx_core",
  "fx_core sp_poly_shr" => "an IO takes fx_io_core before fx_core",
  "fx_fast_call sp_poly_shl" => "the receiver is an Integer",
  "fx_fast_call sp_poly_shr" => "the receiver is an Integer",
  "fx_fiber_class sp_Queue_push" => "a new Queue has no bound, so the push does not wait",
}

KEYWORDS = Set.new(%w[if else for while do switch return sizeof case defined
                      typedef struct union enum goto break continue])

# A function definition starts at the left margin with a type, and its
# parameter list ends in `{`, on the same line or the next one.
HEAD = /\A([A-Za-z_][\w \t*]*?)\b([A-Za-z_]\w*)\s*\(/

# The text with its comments and string literals blanked, line for line.
# The braces of an #else or #elif branch are blanked too, so that the brace
# count follows the first branch of a conditional alone; the calls of every
# branch stay.
def code_of(text)
  code = text.gsub(%r{/\*.*?\*/}m) { |c| c.gsub(/[^\n]/, " ") }
             .gsub(%r{//[^\n]*}, "")
             .gsub(/"(?:\\.|[^"\\\n])*"/, '""')
             .gsub(/'(?:\\.|[^'\\\n])'/, "''")
  branch = []   # one entry per open #if: true inside its first branch
  code.lines.map do |l|
    case l
    when /\A\s*#\s*if/ then branch.push(true); l
    when /\A\s*#\s*el(se|if)/ then branch[-1] = false unless branch.empty?; l
    when /\A\s*#\s*endif/ then branch.pop; l
    else branch.include?(false) ? l.tr("{}", "  ") : l
    end
  end.join
end

# Yields [name, body, line] for each C function definition. The body runs
# from its `{` to the matching `}`, with comments and strings blanked.
def each_function(text)
  lines = code_of(text).lines
  i = 0
  while i < lines.size
    l = lines[i]
    if (m = HEAD.match(l)) && !KEYWORDS.include?(m[2]) && !m[2].match?(/\A[A-Z]/) &&
       !l.start_with?("#") && !l.include?("=")
      # the head may span lines up to the `{`; a `;` before it makes it a
      # prototype, and statements may follow the `{` on its line
      j = i
      head = l.dup
      while !head.include?("{") && !head.include?(";") && j + 1 < lines.size && j - i < 6
        j += 1
        head << lines[j]
      end
      if head.include?("{") && !head[0...head.index("{")].include?(";")
        depth = 0
        k = j
        body = []
        started = false
        while k < lines.size
          line = lines[k]
          body << line
          depth += line.count("{")
          started ||= depth > 0
          depth -= line.count("}")
          break if started && depth <= 0
          k += 1
        end
        yield m[2], body.join, j + 1
        i = k
      end
    end
    i += 1
  end
end

def calls(body)
  body.scan(/\b([A-Za-z_]\w*)\s*\(/).flatten.to_set - KEYWORDS
end

# ---- 1. the runtime functions that reach a park ----

graph = Hash.new { |h, k| h[k] = Set.new }
Dir[File.join(SPINEL, "lib", "*.{c,h}")].sort.each do |path|
  each_function(File.read(path)) { |name, body, _| graph[name].merge(calls(body)) }
end

stops = ->(name) { STOP.keys.any? { |re| name.match?(re) } }
parks = SINKS.to_set
loop do
  more = graph.each_key.select { |name| !parks.include?(name) && !stops.(name) && graph[name].intersect?(parks) }
  break if more.empty?
  parks.merge(more)
end

# ---- 2. the calls of Fluxpinel ----

findings = []
FILES.each do |path|
  text = File.read(path)
  each_function(text) do |name, body, line0|
    next if ALLOW.key?(name) || name == "FX_UNLOCKED"
    depth = 0   # of FX_UNLOCKED( ... ) around the current position
    body.each_line.with_index(line0) do |line, lineno|
      line.scan(/\bFX_UNLOCKED\s*\(|\b(sp_\w+)\s*\(|[()]/) do
        tok = $~[0]
        callee = $~[1]
        if tok.start_with?("FX_UNLOCKED")
          depth = 1
        elsif callee
          wrapped = depth > 0
          depth += 1 if wrapped   # the `(` of the call
          next if wrapped || !parks.include?(callee) || ALLOW.key?("#{name} #{callee}")
          findings << "#{path}:#{lineno}: #{name} calls #{callee} with the walker lock"
        elsif depth > 0
          depth += tok == "(" ? 1 : -1
        end
      end
    end
  end
end

File.write(ENV["PARK_AUDIT_TABLE"], parks.sort.join("\n") + "\n") if ENV["PARK_AUDIT_TABLE"]

# PARK_AUDIT_WHY=NAME,... prints the path of each function to a sink, or
# what it calls when it reaches none.
(ENV["PARK_AUDIT_WHY"] || "").split(",").each do |name|
  path = [name]
  until SINKS.include?(path.last) || path.size > 20
    nxt = graph[path.last].find { |c| parks.include?(c) && !path.include?(c) }
    break unless nxt
    path << nxt
  end
  if SINKS.include?(path.last) then puts "#{name}: #{path.join(' -> ')}"
  elsif !graph.key?(name) then puts "#{name}: not a function of the runtime"
  else puts "#{name}: reaches no park; calls #{graph[name].sort.join(' ')}"
  end
end

puts findings
puts "park_audit: #{parks.size} runtime functions reach a park; #{findings.size} calls keep the walker lock"
exit findings.empty? ? 0 : 1
