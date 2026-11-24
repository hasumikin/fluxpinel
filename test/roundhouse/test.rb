# Runs the host build of the blog and checks the pages, the console and the
# IRB of --irb-port:
#
#   ruby test/roundhouse/test.rb BLOG HOST
#
# BLOG is the Spinel output of the blog after apply.rb, with its assets.
# HOST is the host binary. The server runs in BLOG on free ports, with a
# copy of db/seed.sql in a temporary database.
require_relative "helper"

blog, host = ARGV
abort "usage: ruby #{$0} BLOG HOST" unless host
host = File.expand_path(host)

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
  # --irb-port is not the first argument: the host takes it at any place,
  # and the blog gets its -b
  pid = spawn(env, host, "-b", "127.0.0.1", "--irb-port", irb.to_s, chdir: blog, [:out, :err] => log)
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
      # the backtrace of an error has no frame of the listener
      sock.write("Article.find(99)\r")
      screen = irb_screen(sock, "irb(main):003")
      check "an error shows with no frame of the listener",
        screen && screen.include?("(ActiveRecord::RecordNotFound)") && !screen.include?("remote_irb.rb")
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
