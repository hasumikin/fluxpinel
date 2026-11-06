# remote_irb.rb -- IRB on a telnet connection: `fluxpinel --irb-port PORT`
# listens on 127.0.0.1:PORT, and `telnet localhost PORT` opens IRB in the
# running program, with the line editor of Reline on the terminal of the
# client.
#
#   Fluxpinel::RemoteIRB.listen(port)   starts the listener thread
#   Fluxpinel::RemoteIRB.serve(socket)  runs one session on a socket
#
# One session runs at a time; a second connection reads "busy" and is
# closed. $stdout and $stderr of the thread of the session go to the
# client (the thread-local slots that src/global.c reads); the other
# threads of the program keep theirs. Reline needs io_gate= and
# Reline::Telnet (a Reline after 0.7.0); with an older Reline, listen warns
# and does nothing.

require "socket"
require "irb"

module Fluxpinel
  module RemoteIRB
    # The keys of the connection, as the ANSI gate of Reline reads them.
    # Reline::Telnet does the negotiation and takes the telnet sequences
    # out; a reader thread (RemoteIRB.read_keys) feeds it each chunk and
    # puts the keys in the queue as an Array, with :eof at the end, so that
    # Ctrl-C reaches the session while it evaluates and nobody reads keys.
    class Input
      def initialize(queue)
        @q = queue
        @buf = []
        @eof = false
      end

      def tty? = false
      def external_encoding = Encoding::UTF_8

      def wait_readable(timeout = nil)
        return self unless @buf.empty?
        return self if @eof
        take(timeout) ? self : nil
      end

      def getbyte
        while @buf.empty?
          return nil if @eof
          take(nil)
        end
        @buf.shift
      end

      # Reline.input= asks respond_to?(:getc) only
      def getc
        b = getbyte
        b && b.chr
      end

      def readpartial(n)
        take(nil) if @buf.empty? && !@eof
        @buf.shift(n).pack("C*")
      end

      private

      # Takes the next chunk; false when timeout passed with none.
      def take(timeout)
        item = timeout ? @q.pop(timeout: timeout) : @q.pop
        return false if item.nil?
        if item == :eof
          @eof = true
        else
          @buf.concat(item)
        end
        true
      end
    end

    # The $stdout of a session: a newline becomes CR LF, as the terminal
    # of the client expects.
    class Output
      def initialize(sock, telnet)
        @sock = sock
        @telnet = telnet
      end

      def write(*strs)
        n = 0
        strs.each do |s|
          s = s.to_s
          @sock.write(s.gsub("\n", "\r\n"))
          n += s.bytesize
        end
        n
      end

      def <<(s)
        write(s)
        self
      end

      def print(*args) = (write(args.join); nil)
      def printf(*args) = (write(format(*args)); nil)

      def puts(*args)
        return write("\n") if args.empty?
        args.flatten.each do |a|
          s = a.to_s
          write(s.end_with?("\n") ? s : s + "\n")
        end
        nil
      end

      def flush = self
      def sync = true

      def sync=(_v)
        true
      end

      # the terminal of the client takes colors
      def tty? = true
      def external_encoding = Encoding::UTF_8
      def internal_encoding = nil
      def set_encoding(*) = self
      def winsize = (@telnet.rows ? [@telnet.rows, @telnet.columns] : [24, 80])
    end

    # The input method of the session: Reline on the connection, with the
    # size the client reported.
    class InputMethod < IRB::RelineInputMethod
      def initialize(completor, input, sock, telnet)
        super(completor)
        @stdin = input
        @stdout = sock
        @telnet = telnet
      end

      def winsize = (@telnet.rows ? [@telnet.rows, @telnet.columns] : [24, 80])

      # IRB rescues the LoadError of `require 'rdoc'` only; rdoc 8.1
      # requires rbs when the ri driver starts, which Fluxpinel cannot
      # load. With no driver the completion dialog comes with no document.
      def rdoc_ri_driver
        super
      rescue LoadError
        @rdoc_ri_driver = nil
      end
    end

    @server = nil
    @busy = false

    class << self
      # The port of the listener, or nil.
      def port
        @server&.addr&.[](1)
      end

      # Listens on host:port and serves each connection in a thread of its
      # own. Answers the thread, or nil when Reline cannot run on a socket.
      def listen(port, host: "127.0.0.1")
        unless Reline.respond_to?(:io_gate=) && defined?(Reline::Telnet)
          warn "fluxpinel: --irb-port needs a Reline with io_gate= and Reline::Telnet (Reline #{Reline::VERSION} has none)"
          return nil
        end
        setup
        @server = TCPServer.new(host, Integer(port))
        Thread.new do
          loop do
            sock = @server.accept
            if @busy
              sock.write "busy\r\n"
              sock.close
              next
            end
            begin
              @busy = true
              serve(sock)
            rescue StandardError, ScriptError => e
              warn "fluxpinel: remote irb: #{e.class}: #{e.message}"
            ensure
              @busy = false
              sock.close rescue nil
            end
          end
        end
      end

      # The settings of every session: the line editor on a terminal that
      # is not a tty of the process, and no pager (it reads the process's
      # STDIN).
      def setup
        return if @setup
        @setup = true
        IRB.setup(nil, argv: [])
        IRB.conf[:USE_MULTILINE] = true
        IRB.conf[:PROMPT_MODE] = :DEFAULT
        IRB.conf[:USE_PAGER] = false
      end

      # Reads sock until its end, in a thread: the data bytes go to queue,
      # and Ctrl-C interrupts the session as SIGINT does in irb: the line
      # editor drops the line at the prompt (Reline.line_editor.interrupt),
      # or the evaluation stops (IRB.irb_abort, an Abort in the thread of
      # the session).
      def read_keys(sock, telnet, queue, session)
        telnet.on_interrupt = lambda do
          irb = session[:irb]
          next unless irb
          if irb.instance_variable_get(:@signal_status) == :IN_INPUT
            Reline.line_editor.interrupt
          else
            IRB.irb_abort(irb)
          end
        end
        Thread.new do
          begin
            loop do
              data = telnet.feed(sock.readpartial(1024))
              queue << data unless data.empty?
            end
          rescue EOFError, IOError, SystemCallError
            # the connection ended
          ensure
            queue << :eof
          end
        end
      end

      # One session of IRB on sock, until exit or the end of the connection.
      def serve(sock)
        setup
        telnet = Reline::Telnet.new(sock)   # negotiates; its on_resize sets Reline.screen_size
        queue = Queue.new
        input = Input.new(queue)
        session = {}
        reader = read_keys(sock, telnet, queue, session)
        gate = Reline::ANSI.new(input: input, output: sock, terminal: true)
        Reline.io_gate = gate
        # IRB 1.18 asks the constant Reline::IOGate for in_pasting?: the
        # default gate on the process's STDIN says "pasting" when STDIN is
        # at EOF, and Enter then never submits
        default_gate = Reline::IOGate
        Reline.send(:remove_const, :IOGate)
        Reline.const_set(:IOGate, gate)
        # $stdout and $stderr of this thread go to the client (src/global.c
        # reads these slots first); the other threads of the program keep
        # theirs. The messages of the start of a session (inspect mode) go
        # to the client too.
        Thread.current[:fluxpinel_stdout] = Thread.current[:fluxpinel_stderr] = Output.new(sock, telnet)
        begin
          im = InputMethod.new(IRB::RegexpCompletor.new, input, sock, telnet)
          irb = IRB::Irb.new(nil, im)
          IRB.conf[:MAIN_CONTEXT] = irb.context
          session[:irb] = irb
          irb.run(IRB.conf)
        ensure
          session[:irb] = nil
          Thread.current[:fluxpinel_stdout] = Thread.current[:fluxpinel_stderr] = nil
          sock.close rescue nil
          reader.join
          Reline.io_gate = nil
          Reline.send(:remove_const, :IOGate)
          Reline.const_set(:IOGate, default_gate)
        end
      end
    end
  end
end
