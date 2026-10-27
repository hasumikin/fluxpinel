# Runs the host build of the blog and checks the pages and the console:
#
#   ruby test/roundhouse/test.rb BLOG HOST
#
# BLOG is the Spinel output of the blog after apply.rb, with its assets.
# HOST is the host binary. The server runs in BLOG on free ports, with a
# copy of db/seed.sql in a temporary database.
require "socket"
require "net/http"
require "tmpdir"

blog, host = ARGV
abort "usage: ruby #{$0} BLOG HOST" unless host
host = File.expand_path(host)
$failed = false

def free_port
  server = TCPServer.new("127.0.0.1", 0)
  server.addr[1]
ensure
  server&.close
end

def check(what, ok)
  puts "#{ok ? "ok" : "NG"} #{what}"
  $failed = true unless ok
end

# Waits until port accepts. Stops when the server exits first.
def wait_port(port, pid, log)
  deadline = Time.now + 60
  loop do
    return TCPSocket.new("127.0.0.1", port).close
  rescue SystemCallError
    abort "test: the server exited\n" + File.read(log).lines.last(20).join if Process.waitpid(pid, Process::WNOHANG)
    abort "test: no server on port #{port} after 60 seconds" if Time.now > deadline
    sleep 0.2
  end
end

def get(port, path)
  Net::HTTP.start("127.0.0.1", port, read_timeout: 10) { |h| h.get(path) }
end

# The text the console writes before its next prompt.
def reply(sock)
  buf = +""
  until buf.end_with?("fx> ")
    abort "test: no prompt from the console, got #{buf.inspect}" unless sock.wait_readable(10)
    buf << sock.readpartial(4096)
  end
  buf.delete_suffix("fx> ")
end

def eval_line(sock, line)
  sock.write(line + "\n")
  reply(sock)
end

Dir.mktmpdir("fx-blog") do |dir|
  db = File.join(dir, "blog.sqlite3")
  log = File.join(dir, "server.log")
  system("sqlite3", db, in: File.join(blog, "db/seed.sql"), exception: true)
  http = free_port
  console = free_port
  env = { "PORT" => http.to_s, "BLOG_DB" => db, "FLUXPINEL_CONSOLE" => console.to_s }
  pid = spawn(env, host, chdir: blog, [:out, :err] => log)
  begin
    wait_port(http, pid, log)
    wait_port(console, pid, log)

    res = get(http, "/articles")
    check "GET /articles answers 200", res.code == "200"
    check "GET /articles has the seed article", res.body.to_s.include?("Getting Started with Rails")

    sock = TCPSocket.new("127.0.0.1", console)
    reply(sock)
    check "the console counts the articles", eval_line(sock, 'Console.count("Article")') == "=> 3\n"
    before = eval_line(sock, 'Console.count("Comment")')[/\A=> (\d+)\n\z/, 1].to_i
    eval_line(sock, 'Console.create("Comment", {article_id: 1, commenter: "fx", body: "from the fx test"})')
    check "the console creates a comment",
      eval_line(sock, 'Console.count("Comment")') == "=> #{before + 1}\n"
    eval_line(sock, 'a = Console.find("Article", 1)')
    check "the console keeps a local",
      eval_line(sock, "a.instance_variable_get(:@title)") == "=> \"Getting Started with Rails\"\n"
    sock.write("exit\n")
    check "exit closes the session", sock.wait_readable(10) && sock.read == ""
    sock.close

    res = get(http, "/articles/1")
    check "the page of the article has the new comment", res.body.to_s.include?("from the fx test")

    # a session that computes for a while holds one worker: the pages and a
    # second session go on meanwhile
    busy = TCPSocket.new("127.0.0.1", console)
    reply(busy)
    other = TCPSocket.new("127.0.0.1", console)
    reply(other)
    busy.write("s = 0; 3_000_000.times { |i| s += i }; s\n")
    t0 = Time.now
    res = get(http, "/articles")
    check "a page answers while a session computes", res.code == "200" && Time.now - t0 < 2
    t0 = Time.now
    check "a second session answers while a session computes",
      eval_line(other, "6 * 7") == "=> 42\n" && Time.now - t0 < 2
    check "the busy session answers its sum", reply(busy) == "=> 4499998500000\n"
    busy.close
    other.close

    check "the server runs after the session", Process.waitpid(pid, Process::WNOHANG).nil?
  ensure
    begin
      Process.kill(:TERM, pid)
      unless 30.times.any? { sleep 0.1; Process.waitpid(pid, Process::WNOHANG) }
        Process.kill(:KILL, pid)
        Process.wait(pid)
      end
    rescue Errno::ESRCH, Errno::ECHILD
      # the server has exited, and wait_port has reaped it
    end
    puts File.read(log).lines.last(20).join if $failed
  end
end
exit 1 if $failed
