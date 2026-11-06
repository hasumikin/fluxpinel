# remote_irb.rb -- IRB on a telnet connection: `fluxpinel --irb-port PORT`
# listens on 127.0.0.1:PORT, and `telnet localhost PORT` opens IRB in the
# running program, with the line editor of Reline on the terminal of the
# client.
#
#   Fluxpinel::RemoteIRB.listen(port)   starts the listener thread
#   Fluxpinel::RemoteIRB.serve(socket)  runs one session on a socket
#
# One session runs at a time; a second connection reads "busy" and is
# closed. During a session $stdout and $stderr of the program go to the
# client. Reline needs io_gate= (a Reline after 0.7.0); with an older
# Reline, listen warns and does nothing.

require "socket"
require "irb"

module Fluxpinel
  module RemoteIRB
    IAC = 255
    DONT = 254
    DO = 253
    WONT = 252
    WILL = 251
    SB = 250
    SE = 240
    OPT_BINARY = 0
    OPT_ECHO = 1
    OPT_SGA = 3
    OPT_NAWS = 31

    # The telnet layer of a connection: the negotiation, and the IAC
    # sequences taken out of the byte stream. A sequence that ends past a
    # chunk waits in the carry for the next chunk.
    class Telnet
      attr_reader :rows, :cols
      attr_accessor :on_naws

      def initialize(sock)
        @sock = sock
        @carry = +""
        @rows = @cols = nil
        @on_naws = nil
      end

      # The server echoes, sends each key as it comes, takes the window
      # size and passes 8-bit data both ways.
      def negotiate
        @sock.write [IAC, WILL, OPT_ECHO, IAC, WILL, OPT_SGA, IAC, DO, OPT_NAWS,
                     IAC, WILL, OPT_BINARY, IAC, DO, OPT_BINARY].pack("C*")
      end

      # Answers the data bytes of chunk, as an Array of Integers.
      def feed(chunk)
        buf = @carry + chunk
        @carry = +""
        data = []
        i = 0
        while i < buf.bytesize
          b = buf.getbyte(i)
          if b != IAC
            data << b
            i += 1
            next
          end
          cmd = buf.getbyte(i + 1)
          if cmd.nil?
            @carry = buf.byteslice(i..)
            break
          end
          case cmd
          when IAC
            data << IAC
            i += 2
          when DONT, DO, WONT, WILL
            opt = buf.getbyte(i + 2)
            if opt.nil?
              @carry = buf.byteslice(i..)
              break
            end
            answer(cmd, opt)
            i += 3
          when SB
            e = buf.index([IAC, SE].pack("C*"), i)
            if e.nil?
              @carry = buf.byteslice(i..)
              break
            end
            subneg(buf.byteslice(i + 2, e - i - 2))
            i = e + 2
          else
            i += 2
          end
        end
        data
      end

      private

      # an option the server did not offer is refused
      def answer(cmd, opt)
        case cmd
        when WILL
          @sock.write [IAC, DONT, opt].pack("C*") unless [OPT_NAWS, OPT_BINARY].include?(opt)
        when DO
          @sock.write [IAC, WONT, opt].pack("C*") unless [OPT_ECHO, OPT_SGA, OPT_BINARY].include?(opt)
        end
      end

      def subneg(body)
        return unless body.getbyte(0) == OPT_NAWS && body.bytesize >= 5
        @cols = (body.getbyte(1) << 8) | body.getbyte(2)
        @rows = (body.getbyte(3) << 8) | body.getbyte(4)
        @on_naws&.call(@rows, @cols)
      end
    end

    # The keys of the connection, as the ANSI gate of Reline reads them:
    # the IAC sequences are gone, and a size report reaches Reline.
    class Input
      def initialize(sock, telnet)
        @sock = sock
        @telnet = telnet
        @buf = []
      end

      def tty? = false
      def external_encoding = Encoding::UTF_8

      def wait_readable(timeout = nil)
        return self unless @buf.empty?
        @sock.wait_readable(timeout)
      end

      def getbyte
        fill while @buf.empty?
        @buf.shift
      rescue EOFError, SystemCallError
        nil
      end

      # Reline.input= asks respond_to?(:getc) only
      def getc
        b = getbyte
        b && b.chr
      end

      def readpartial(n)
        fill if @buf.empty?
        @buf.shift(n).pack("C*")
      end

      private

      def fill
        @buf.concat @telnet.feed(@sock.readpartial(1024))
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
      def winsize = (@telnet.rows ? [@telnet.rows, @telnet.cols] : [24, 80])
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

      def winsize = (@telnet.rows ? [@telnet.rows, @telnet.cols] : [24, 80])

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
        unless Reline.respond_to?(:io_gate=)
          warn "fluxpinel: --irb-port needs a Reline with io_gate= (Reline #{Reline::VERSION} has none)"
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

      # One session of IRB on sock, until exit or the end of the connection.
      def serve(sock)
        setup
        telnet = Telnet.new(sock)
        telnet.negotiate
        input = Input.new(sock, telnet)
        telnet.on_naws = ->(rows, cols) { Reline.screen_size = [rows, cols] }
        gate = Reline::ANSI.new(input: input, output: sock, terminal: true)
        Reline.io_gate = gate
        # IRB 1.18 asks the constant Reline::IOGate for in_pasting?: the
        # default gate on the process's STDIN says "pasting" when STDIN is
        # at EOF, and Enter then never submits
        default_gate = Reline::IOGate
        Reline.send(:remove_const, :IOGate)
        Reline.const_set(:IOGate, gate)
        # the messages of the start of a session (inspect mode) go to the
        # client too
        saved = [$stdout, $stderr]
        $stdout = $stderr = Output.new(sock, telnet)
        begin
          im = InputMethod.new(IRB::RegexpCompletor.new, input, sock, telnet)
          irb = IRB::Irb.new(nil, im)
          IRB.conf[:MAIN_CONTEXT] = irb.context
          irb.run(IRB.conf)
        ensure
          $stdout, $stderr = saved
          Reline.io_gate = nil
          Reline.send(:remove_const, :IOGate)
          Reline.const_set(:IOGate, default_gate)
        end
      end
    end
  end
end
