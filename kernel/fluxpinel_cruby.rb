# fluxpinel_cruby.rb -- kernel/fluxpinel.rb under CRuby.
#
#   ruby -r ./kernel/fluxpinel_cruby.rb app.rb
#
# native_func gives each function of fluxpinel.rb the CRuby code that does what
# Fluxpinel does in the host build, so the same program runs under CRuby. Each
# load and eval has locals of its own, with main as self.

require "prism"

module FluxpinelCRuby
  # a new binding at the top level
  def self.fresh = TOPLEVEL_BINDING.eval("proc { binding }").call

  # Fluxpinel.session and Fluxpinel.feed (src/console.c): the same pieces,
  # replies and prompts, with one binding for the locals of the session.
  class Session
    def initialize
      @binding = FluxpinelCRuby.fresh
      @binding.local_variable_set(:_, nil)
      @buf = +""
      @line = 1
    end

    def feed(line)
      @buf << line
      @buf << "\n" unless @buf.end_with?("\n")
      result = Prism.parse(@buf, filepath: "(console)", line: @line, scopes: [@binding.local_variables])
      state = Session.state(result, @buf)
      return "fx* " if state == :more
      piece = @buf
      first = @line
      @line += piece.count("\n")
      @buf = +""
      if state == :error
        e = result.errors.first
        return "(console):#{e.location.start_line}: #{e.message} (SyntaxError)\nfx> "
      end
      return "fx> " if piece == "\n"
      run(piece, first)
    end

    # the rule of fx_session_state
    def self.state(result, buf)
      return :done if result.errors.empty?
      n = buf.rstrip.bytesize
      more = result.errors.any? { |e| e.location.start_offset >= n || e.type.to_s =~ /_term(_|\z)|_eof\z/ }
      stray = result.errors.any? do |e|
        %i[unexpected_token_ignore expect_eol_after_statement].include?(e.type) && e.location.start_offset < n
      end
      more && !stray ? :more : :error
    end

    private

    def run(piece, first)
      v = @binding.eval(piece, "(console)", first)
      @binding.local_variable_set(:_, v)
      "=> #{v.inspect}\nfx> "
    rescue SystemExit
      raise
    rescue Exception => e
      "#{e.message} (#{e.class})\nfx> "
    end
  end

  # fluxpinel --console
  def self.console_stdin
    s = Session.new
    print "fx> "
    $stdout.flush
    while (line = $stdin.gets)
      print s.feed(line)
      $stdout.flush
    end
    print "\n"
  end

  FUNCS = {
    "fx_api_load" => ->(path) { FluxpinelCRuby.fresh.eval(File.read(path), path) },
    "fx_api_eval" => ->(src) { FluxpinelCRuby.fresh.eval(src) },
    "fx_api_call" => ->(obj, name, args) { obj.public_send(name, *args) },
    "fx_api_session" => -> { Session.new },
    "fx_api_feed" => ->(session, line) { session.feed(line) }
  }.freeze
end

class Module
  def native_func(name, _args, _ret, cname)
    define_singleton_method(name, &FluxpinelCRuby::FUNCS.fetch(cname))
  end
end
