# Runs the host build of Campfire and checks the pages, the console and the
# IRB of --irb-port:
#
#   ruby test/roundhouse/campfire.rb CAMPFIRE HOST
#
# CAMPFIRE is the Spinel output of Campfire after apply.rb. HOST is the host
# binary. The server runs in CAMPFIRE on free ports, with a new database in
# a temporary directory. The server makes the tables at its start.
require_relative "helper"

campfire, host = ARGV
abort "usage: ruby #{$0} CAMPFIRE HOST" unless host
host = File.expand_path(host)

Dir.mktmpdir("fx-campfire") do |dir|
  db = File.join(dir, "campfire.sqlite3")
  log = File.join(dir, "server.log")
  http = free_port
  console = free_port
  env = { "PORT" => http.to_s, "BLOG_DB" => db, "FLUXPINEL_CONSOLE" => console.to_s }
  pid = spawn(env, host, chdir: campfire, [:out, :err] => log)
  begin
    wait_port(http, pid, log)
    wait_port(console, pid, log)

    # with no account, each page goes to the first run
    res = get(http, "/first_run")
    check "GET /first_run answers 200", res.code == "200" && res.body.to_s.include?("<form")
    check "GET / goes to the sign in", get(http, "/")["location"].to_s.end_with?("/session/new")
    check "GET /session/new goes to the first run",
      get(http, "/session/new")["location"].to_s.end_with?("/first_run")

    sock = TCPSocket.new("127.0.0.1", console)
    reply(sock)
    check "the console counts the accounts", eval_line(sock, 'Console.count("Account")') == "=> 0\n"
    eval_line(sock, 'acct = Account.create!(name: "Fluxpinel")')
    check "the console creates an account",
      eval_line(sock, "[Account.count, acct.name, acct.join_code.size]") == "=> [1, \"Fluxpinel\", 14]\n"

    eval_line(sock, 'u = User.create!(name: "Fx One", email_address: "one@example.com", password: "secret123456")')
    eval_line(sock, 'u2 = User.create!(name: "Fx Two", email_address: "two@example.com", password: "secret123456")')
    check "the console creates users", eval_line(sock, "[User.count, u.name, u.role]") == "=> [2, \"Fx One\", \"member\"]\n"
    check "the console finds a user by a column",
      eval_line(sock, 'User.find_by(email_address: "two@example.com").name') == "=> \"Fx Two\"\n" &&
      eval_line(sock, 'User.where(name: "Fx Two").to_a.map(&:id)') == "=> [2]\n"
    check "a user checks its password",
      eval_line(sock, '[u.authenticate("secret123456") ? true : false, u.authenticate("wrong") ? true : false]') ==
        "=> [true, false]\n"

    # the rooms are one table (single table inheritance). An open room gives
    # a membership to each user, a closed room gives none.
    eval_line(sock, 'open = Rooms::Open.create!(name: "Lobby", creator: u)')
    eval_line(sock, 'closed = Rooms::Closed.create!(name: "Staff", creator: u)')
    check "the console creates rooms of two classes",
      eval_line(sock, "[Room.count, open.type, Room.find(closed.id).name]") == "=> [2, \"Rooms::Open\", \"Staff\"]\n"
    check "an open room has each user",
      eval_line(sock, "[open.memberships.count, open.users.map(&:name)]") == "=> [2, [\"Fx One\", \"Fx Two\"]]\n"
    check "a second membership of a user in a room raises",
      eval_line(sock, "Membership.create!(room: open, user: u)").include?("ActiveRecord::RecordNotUnique")
    check "the console creates a membership",
      eval_line(sock, "[closed.memberships.count, Membership.create!(room: closed, user: u2).persisted?]") ==
        "=> [0, true]\n" &&
      eval_line(sock, "[closed.users.map(&:name), u2.rooms.map(&:name).sort, Membership.count]") ==
        "=> [[\"Fx Two\"], [\"Lobby\", \"Staff\"], 3]\n"

    # the body of a message is rich text (Action Text), and the search reads
    # an index of full text
    eval_line(sock, 'msg = Message.create!(room: open, creator: u, body: "hello from the console", client_message_id: "fx-1")')
    check "the console creates a message",
      eval_line(sock, "[Message.count, msg.room.name, msg.creator.name, open.messages.count]") ==
        "=> [1, \"Lobby\", \"Fx One\", 1]\n"
    check "a message has its plain text", eval_line(sock, "msg.plain_text_body") == "=> \"hello from the console\"\n"
    check "the search finds a message",
      eval_line(sock, '[Message.search("hello").count, Message.search("absent").count]') == "=> [1, 0]\n"
    eval_line(sock, "sgid = u2.attachable_sgid")
    eval_line(sock, 'body = "<div>hi <action-text-attachment sgid=\"#{sgid}\" ' \
      'content-type=\"application/vnd.campfire.mention\"></action-text-attachment></div>"')
    eval_line(sock, 'mention = Message.create!(room: open, creator: u, body: body, client_message_id: "fx-2")')
    check "a message has the users that it mentions",
      eval_line(sock, "[mention.mentionees.map(&:name), mention.plain_text_body]") ==
        "=> [[\"Fx Two\"], \"hi @Fx Two\"]\n" &&
      eval_line(sock, "msg.mentionees.to_a.size") == "=> 0\n"

    eval_line(sock, 'boost = Boost.create!(message: msg, booster: u2, content: "+1")')
    check "the console creates a boost",
      eval_line(sock, "[Boost.count, msg.boosts.count, boost.booster.name]") == "=> [1, 1, \"Fx Two\"]\n"
    eval_line(sock, 's = Session.create!(user: u, user_agent: "fx", ip_address: "127.0.0.1")')
    check "the console creates a session",
      eval_line(sock, "[Session.count, s.user.name, s.token.size, u.sessions.count]") == "=> [1, \"Fx One\", 24, 1]\n"

    eval_line(sock, 'u.update!(name: "Fx Uno")')
    check "the console updates a record", eval_line(sock, "User.find(u.id).name") == "=> \"Fx Uno\"\n"
    eval_line(sock, "mention.destroy")
    check "the console destroys a record", eval_line(sock, "Message.count") == "=> 1\n"
    sock.write("exit\n")
    check "exit closes the session", sock.wait_readable(10) && sock.read == ""
    sock.close

    # with an account, the pages go to the sign in
    check "GET /first_run goes to the root", get(http, "/first_run").code == "302"
    res = get(http, "/session/new")
    check "GET /session/new answers 200", res.code == "200" && res.body.to_s.include?("<form")

    check "the server runs after the session", Process.waitpid(pid, Process::WNOHANG).nil?
  ensure
    stop(pid)
    puts File.read(log).lines.last(20).join if $failed
  end

  # --irb-port: IRB with the line editor of Reline, on the database that the
  # console wrote. With no Reline that runs on a socket the listener writes
  # a line and is not there.
  irb = free_port
  env = { "PORT" => http.to_s, "BLOG_DB" => db, "HOME" => dir }
  pid = spawn(env, host, "--irb-port", irb.to_s, chdir: campfire, [:out, :err] => log)
  begin
    wait_port(http, pid, log)
    if File.read(log).include?("--irb-port needs a Reline")
      puts "skip the IRB of --irb-port (no Reline that runs on a socket)"
    else
      wait_port(irb, pid, log)
      sock = TCPSocket.new("127.0.0.1", irb)
      check "the IRB of --irb-port reads the records",
        irb_line(sock, "[User.count, Room.first.name]", "=> [2, \"Lobby\"]\r\n")
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
