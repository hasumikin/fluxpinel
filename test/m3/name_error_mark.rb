# The mark under the source of a NameError or NoMethodError, as the gem
# error_highlight writes it in CRuby: after the message, an empty line, the
# line of the raise, and carets under the name with its dot, `&.` or `::`.
# detailed_message carries it, so full_message does; highlight: true puts
# it in bold. A fresh exception, a line that is not ASCII, a name on another
# line than its receiver's dot, `.()` and code from a string have no mark.
def show
  yield
rescue NameError => e
  puts e.detailed_message(highlight: false), "--"
end
show { [1].nosuch_zzz }
show { nosuch_zzz }
show { Nosuch_zzz }
module Foo; end
show { Foo::Nosuch_zzz }
show { Nosuch_zzz::Bar }
show { Foo.nosuch_zzz }
show { x = [1]; x&.nosuch_zzz }
show { o = Object.new; o.nosuch_zzz = 1 }
show { [1].nosuch_zzz(2, 3) }
show { x = nil; x + 1 }
show { x = nil; x += 1 }
show { x = Object.new; x.nosuch_zzz += 1 }
show { nil[1] }
show { [1].send(:nosuch_zzz) }
show { Object.const_get(:Nosuch_zzz) }
show { raise NameError, "by hand" }
show { [1].each { |v| v.nosuch_zzz } }
def in_method = [1].nosuch_zzz
show { in_method }
require_relative "requires/nosuch_mark"
show { marked_in_file }
show { [1]
  .nosuch_zzz }
show { [1].nosuch_zzz(
  2) }
show { [1].nosuch_zzz # ünïcödé
}
show { nil.() }
show {	[1].nosuch_zzz }
show { eval("[1].nosuch_zzz") }
show { eval("[1].nosuch_zzz", binding, "(irb)", 1) }
show { Object.new.instance_eval("nosuch_zzz") }
begin
  [1].nosuch_zzz
rescue => e
end
puts e.clone.detailed_message(highlight: false), e.dup.detailed_message(highlight: false)
d = e.dup
d.set_backtrace(["x:1:in 'y'"])
puts d.full_message(highlight: false)
p e.detailed_message(highlight: true)
p NoMethodError.new("fresh").detailed_message(highlight: false), e.message
