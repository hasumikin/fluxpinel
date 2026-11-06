# undef, undef_method and remove_method: an undef hides the method of a
# superclass too, a remove shows it again (the date package undefines
# DateTime.today).
def t(label)
  r = yield
  puts "#{label}: #{r.inspect}"
rescue => e
  puts "#{label}: #{e.class} #{e.message}"
end

class A
  def a = :a
  def b = :b
  def c = :c
  def self.today = :today
end

class B < A
  undef_method :a
  undef :b
  def c = :c_of_b
  class << self
    undef_method :today
  end
end

t("undef a") { B.new.a }
t("undef b") { B.new.b }
t("a of A") { A.new.a }
t("respond_to?") { [B.new.respond_to?(:a), A.new.respond_to?(:a)] }
t("method_defined?") { [B.method_defined?(:a), B.method_defined?(:c), A.method_defined?(:a)] }
t("instance_methods") { B.instance_methods(false).sort }
t("singleton undef") { B.today }
t("today of A") { A.today }
t("remove c") { B.send(:remove_method, :c); B.new.c }
t("remove missing") { B.send(:remove_method, :zzz) }
t("undef missing") { B.send(:undef_method, :zzz) }
t("undef builtin") { o = Class.new { undef_method :inspect }.new; [o.respond_to?(:inspect), (o.inspect rescue $!.class)] }
t("to_s stays") { Class.new { undef_method :to_s }.new.inspect.start_with?("#<") }
