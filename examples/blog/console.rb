# The console of the blog of Roundhouse, and its entries. Copy this file and
# kernel/fluxpinel.rb into the Spinel output of the blog (see README.md).
#
# Console.listen accepts connections on 127.0.0.1:port, one green thread
# for each session, and gives each line to Fluxpinel.feed. The entries
# below choose the model with a `case`: spinel compiles no lookup of a
# class by a String at run time.
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
    when "Article" then Article.count
    when "Comment" then Comment.count
    else raise ArgumentError, "unknown model: " + model
    end
  end

  #: (String, Hash[Symbol, untyped]) -> untyped
  def self.create(model, attrs)
    case model
    when "Article" then Article.create(attrs)
    when "Comment" then Comment.create(attrs)
    else raise ArgumentError, "unknown model: " + model
    end
  end

  #: (String, Integer) -> untyped
  def self.find(model, id)
    case model
    when "Article" then Article.find(id)
    when "Comment" then Comment.find(id)
    else raise ArgumentError, "unknown model: " + model
    end
  end
end
