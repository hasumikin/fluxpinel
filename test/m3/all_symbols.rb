# Symbol.all_symbols, and the private methods of the main object.
# Fluxpinel interns a Symbol when it is evaluated, so the names are used
# before the list is read.
used = [:puts, :all_symbols]
a = Symbol.all_symbols
p a.class, a.size > used.size, a.include?(:puts), a.include?(:all_symbols), a.all?(Symbol)
"made_at_run_time_#{1 + 1}".to_sym
p Symbol.all_symbols.include?(:made_at_run_time_2)
p private_methods(false).sort
p private_methods.include?(:include), private_methods.include?(:puts), methods.include?(:include)
