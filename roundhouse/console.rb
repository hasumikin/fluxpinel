# The console of the blog of Roundhouse, and its entries. apply.rb copies
# this file and kernel/fluxpinel.rb into the Spinel output of the blog (see
# docs/design.md, section 5.4).
#
# Console.listen accepts connections on 127.0.0.1:port, one green thread
# for each session, and gives each line to Fluxpinel.feed. The entries
# below choose the model with a `case`: spinel compiles no lookup of a
# class by a String at run time. apply.rb writes one `when` for each model
# of the application in place of each `# arms:` line, with the name of the
# model for MODEL.
require_relative "fluxpinel"

module Console
  def self.listen(port)
    server = TCPServer.new("127.0.0.1", port)
    $stderr.puts "fluxpinel console on 127.0.0.1:" + port.to_s
    loop do
      sock = server.accept
      Thread.new(sock) do |s|
        serve(s)
      end
    end
  end

  # nil from Fluxpinel.feed: an exit closed the session
  def self.serve(sock)
    session = Fluxpinel.session
    sock.write("fx> ")
    while (line = sock.gets)
      reply = Fluxpinel.feed(session, line)
      break unless reply
      sock.write(reply)
    end
    sock.close
  end

  #: (String) -> Integer
  def self.count(model)
    case model
    # arms: MODEL.count
    else raise ArgumentError, "unknown model: " + model
    end
  end

  #: (String, Hash[Symbol, untyped]) -> untyped
  def self.create(model, attrs)
    case model
    # arms: MODEL.create(attrs)
    else raise ArgumentError, "unknown model: " + model
    end
  end

  #: (String, Integer) -> untyped
  def self.find(model, id)
    case model
    # arms: MODEL.find(id)
    else raise ArgumentError, "unknown model: " + model
    end
  end
end
