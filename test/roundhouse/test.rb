# Runs the host build of the blog and checks the pages, the console and the
# IRB of --irb-port:
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

# Reads the screen of an IRB of --irb-port until it has text, as ASCII with
# no escape sequence, and answers the cursor position query of Reline as a
# terminal does. Answers false after 20 seconds, or at the end of the
# connection.
def irb_screen(sock, text)
  seen = "".b
  deadline = Time.now + 20
  loop do
    plain = seen.bytes.select { |b| b < 128 }.pack("C*").gsub(/\e\[[0-9;?]*[A-Za-z]/, "")
    return true if plain.include?(text)
    return false if Time.now > deadline
    next unless sock.wait_readable(0.1)
    chunk = sock.readpartial(4096)
    seen << chunk
    sock.write("\e[1;1R") if chunk.include?("\e[6n")
  end
rescue EOFError, SystemCallError
  false
end

# Types a line at the prompt of the IRB, and waits for its answer.
def irb_line(sock, line, answer)
  return false unless irb_screen(sock, "irb(main):001")
  sock.write(line + "\r")
  irb_screen(sock, answer)
end

# Whether the server closes the connection within 10 seconds.
def irb_closed?(sock)
  deadline = Time.now + 10
  loop do
    return false if Time.now > deadline
    next unless sock.wait_readable(0.1)
    sock.readpartial(4096)
  end
rescue EOFError, SystemCallError
  sock.close
  true
end

# Ends the server: TERM, then KILL after 3 seconds.
def stop(pid)
  Process.kill(:TERM, pid)
  unless 30.times.any? { sleep 0.1; Process.waitpid(pid, Process::WNOHANG) }
    Process.kill(:KILL, pid)
    Process.wait(pid)
  end
rescue Errno::ESRCH, Errno::ECHILD
  # the server has exited, and the test has reaped it
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
    check "the console calls the models by name", eval_line(sock, "Article.find(1).title") == "=> \"Getting Started with Rails\"\n"
    check "the console calls the methods of a Relation",
      eval_line(sock, "Article.all.size") == "=> 3\n" && eval_line(sock, "Article.all.limit(2).to_a.size") == "=> 2\n"
    check "the console calls a method with optional and rest parameters",
      eval_line(sock, "Article.where(id: [1, 3]).order(:id).to_a.map(&:id)") == "=> [1, 3]\n" &&
      eval_line(sock, 'Article.where("id > ?", 1).to_a.size') == "=> 2\n"
    # the block of a line goes to a method of a Relation, and a break ends
    # the call
    check "the console passes a block to a method of a Relation",
      eval_line(sock, "Article.all.map { |a| a.id * 10 }") == "=> [10, 20, 30]\n" &&
      eval_line(sock, "Article.where(id: [1, 3]).map(&:id)") == "=> [1, 3]\n" &&
      eval_line(sock, "Article.all.each { |a| break a.title if a.id == 2 }") ==
        "=> \"Understanding MVC Architecture\"\n"
    # new of a model makes a record, which is not in the database before a
    # save
    check "the console makes a record with new",
      eval_line(sock, 'fresh = Article.new(title: "from new"); [fresh.title, fresh.persisted?]') ==
        "=> [\"from new\", false]\n" &&
      eval_line(sock, "Article.count") == "=> 3\n"
    # allocate of a class of the kernel is not a call of Fluxpinel: the
    # line raises, and the session and the server go on
    check "a call that Fluxpinel does not have raises",
      eval_line(sock, "Article.allocate").include?("NotImplementedError") &&
      eval_line(sock, "Article.count") == "=> 3\n"
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
    stop(pid)
    puts File.read(log).lines.last(20).join if $failed
  end

  # --irb-port: IRB with the line editor of Reline, for a telnet client.
  # The listener needs a Reline that runs on a socket (GEM_DIRS_FIRST of
  # the Makefile); with no such Reline it writes a line and is not there.
  system("sqlite3", db, in: File.join(blog, "db/seed.sql"), exception: true) if File.delete(db)
  irb = free_port
  env = { "PORT" => http.to_s, "BLOG_DB" => db, "HOME" => dir }
  pid = spawn(env, host, "--irb-port", irb.to_s, chdir: blog, [:out, :err] => log)
  begin
    wait_port(http, pid, log)
    if File.read(log).include?("--irb-port needs a Reline")
      puts "skip the IRB of --irb-port (no Reline that runs on a socket)"
    else
      wait_port(irb, pid, log)
      sock = TCPSocket.new("127.0.0.1", irb)
      check "the IRB of --irb-port counts the articles", irb_line(sock, "Article.count", "=> 3\r\n")
      # exit as Ruby code is Kernel#exit, not the command of IRB: it ends
      # the session, not the server
      sock.write("(exit)\r")
      check "exit as Ruby code closes the session", irb_closed?(sock)
      check "the server runs after exit as Ruby code",
        Process.waitpid(pid, Process::WNOHANG).nil? && get(http, "/articles").code == "200"
      sock = TCPSocket.new("127.0.0.1", irb)
      check "a second session of IRB answers", irb_line(sock, "Article.find(1).title", "=> \"Getting Started with Rails\"\r\n")
      sock.write("exit\r")
      check "the exit command closes the session", irb_closed?(sock)
      check "the server runs after the exit command", Process.waitpid(pid, Process::WNOHANG).nil?
    end
  ensure
    stop(pid)
    puts File.read(log).lines.last(20).join if $failed
  end
end
exit 1 if $failed
