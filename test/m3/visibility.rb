# private, protected and public: the default of a scope, the names, the
# check of a call with a receiver, and respond_to?.

def t(label)
  r = yield
  puts "#{label}: #{r.inspect}"
rescue NameError, TypeError => e
  puts "#{label}: #{e.class} #{e.message}"
end

class Account
  attr_reader :id

  def initialize(id, balance)
    @id = id
    @balance = balance
  end

  def richer?(other) = balance > other.balance
  def peek(other) = other.secret
  def own = self.secret
  def call_kernel = puts("in")

  protected

  def balance = @balance

  private

  def secret = "s#{@id}"
  attr_reader :hidden

  %i[gen1 gen2].each { |n| define_method(n) { n } }

  public

  def open = "open"
  define_method(:dyn) { :dyn }
end

a = Account.new(1, 10)
b = Account.new(2, 20)
t("public") { [a.id, a.open, a.dyn] }
t("protected inside") { b.richer?(a) }
t("protected outside") { a.balance }
t("private") { a.secret }
t("private self.") { a.own }
t("private other") { a.peek(b) }
t("private attr") { a.hidden }
t("private define_method") { a.gen1 }
t("send") { [a.send(:secret), a.send(:balance), a.__send__(:gen2)] }
t("public_send private") { a.public_send(:secret) }
t("public_send protected") { a.public_send(:balance) }
t("initialize") { a.initialize(3, 4) }
t("kernel inside") { a.call_kernel }
t("kernel outside") { a.puts("x") }
t("kernel on Integer") { 5.puts("x") }
t("respond_to?") { %i[id balance secret initialize puts].map { |n| a.respond_to?(n) } }
t("respond_to? all") { %i[id balance secret initialize puts].map { |n| a.respond_to?(n, true) } }

# names, the forms that answer names, and what each answers
class Names
  def a = :a
  def b = :b
  def c = :c
  def d = :d
  t("private names") { private :a, "b" }
  t("private array") { protected [:c] }
  t("private def") { private def e = :e }
  t("private attr") { private attr_accessor :f }
  t("bad name") { private 1 }
  t("unknown name") { private :zork }
  t("no names") { public }
end

n = Names.new
%i[a b c d e f f=].each { |m| t("names #{m}") { n.respond_to?(m) } }

# a subclass changes the visibility of an inherited method on its own copy
class Base
  def shown = :shown
  private def kept = :kept
end

class Derived < Base
  public :kept
  private :shown
end

t("subclass public") { Derived.new.kept }
t("subclass private") { Derived.new.shown }
t("base unchanged") { [Base.new.shown, Base.new.respond_to?(:kept)] }

# class methods
class Factory
  private_class_method :new
  def self.build = new

  class << self
    def open_cm = :open_cm

    private

    def hidden_cm = :hidden_cm
  end

  def self.call_hidden = hidden_cm
  t("private_class_method") { private_class_method :build }
  public_class_method :build
end

t("private new") { Factory.new }
t("factory") { Factory.build.class }
t("private class << self") { Factory.hidden_cm }
t("class method inside") { Factory.call_hidden }
t("public class method") { Factory.open_cm }

# module_function: a public module method and a private instance method
module Util
  module_function

  def twice(x) = x * 2

  public

  def plain = :plain
end

class UsesUtil
  include Util
  def go = twice(4)
end

t("module function") { Util.twice(3) }
t("instance private") { UsesUtil.new.twice(3) }
t("instance inside") { UsesUtil.new.go }
t("after public") { [UsesUtil.new.plain, Util.respond_to?(:plain)] }

# a reopened builtin class
class Array
  private :first
  def head = first
end

t("builtin private") { [1, 2].first }
t("builtin inside") { [1, 2].head }

# a top-level def is private; method_missing takes a private call
def top_helper = :top

class Hooked
  def method_missing(name, *args) = name == :quiet ? "hook #{name}" : super
  private def quiet = :quiet
  private def loud = :loud
end

t("top-level") { 5.top_helper }
t("top-level send") { 5.send(:top_helper) }
t("top-level object") { Object.new.top_helper }
t("hook") { Hooked.new.quiet }
t("hook super") { Hooked.new.loud }
