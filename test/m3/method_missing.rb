# method_missing as the hook of a missing method, respond_to_missing?, and
# the words of NoMethodError.

def t(label)
  r = yield
  puts "#{label}: #{r.inspect}"
rescue NameError => e
  puts "#{label}: #{e.class} #{e.message}"
end

class Ghost
  def initialize = @log = []
  attr_reader :log

  def method_missing(name, *args, &blk)
    if name.to_s.start_with?("get_")
      @log << name
      v = name.to_s.sub("get_", "")
      blk ? blk.call(v, *args) : [v, args]
    else
      super
    end
  end

  def respond_to_missing?(name, include_all = false)
    name.to_s.start_with?("get_") || super
  end

  def real = :real
end

g = Ghost.new
t("ghost") { g.get_name }
t("ghost args") { g.get_size(1, 2) }
t("ghost block") { g.get_x(3) { |v, n| v * n } }
t("real method") { g.real }
t("log") { g.log }
t("send") { g.send(:get_sent, :a) }
t("public_send") { g.public_send(:get_pub) }
t("super") { g.other }
t("super args") { g.other(1, 2) }
t("respond_to?") { [g.respond_to?(:get_any), g.respond_to?(:real), g.respond_to?(:other)] }
t("respond_to? all") { [g.respond_to?(:get_any, true), g.respond_to?(:other, true)] }

class SubGhost < Ghost
  def helper = get_inner
end
t("inherited") { SubGhost.new.get_sub }
t("self call") { SubGhost.new.helper }

# a hook that defines the method, so the next call does not miss
class Lazy
  def method_missing(name, *args)
    return super unless name.end_with?("!")
    self.class.define_method(name) { "#{name} defined" }
    "#{name} first"
  end
end

l = Lazy.new
t("lazy first") { l.go! }
t("lazy second") { l.go! }
t("lazy missing") { l.stop }

class Config
  def self.method_missing(name, *args) = name.to_s.end_with?("_path") ? "/etc/#{name}" : super
  def self.respond_to_missing?(name, all) = name.to_s.end_with?("_path")
end
t("class hook") { Config.log_path }
t("class super") { Config.other }
t("class respond_to?") { [Config.respond_to?(:x_path), Config.respond_to?(:x)] }

class Quiet
  def method_missing(name, *args) = super
  def vcall = zork
  def call = zork()
end
t("no hook") { Object.new.zork }
t("vcall super") { Quiet.new.vcall }
t("call super") { Quiet.new.call }

module Mod; end
t("nil") { nil.zork }
t("true") { true.zork }
t("integer") { 5.zork }
t("class") { Ghost.zork }
t("module") { Mod.zork }
t("main") { zork }
t("main call") { zork(1) }
