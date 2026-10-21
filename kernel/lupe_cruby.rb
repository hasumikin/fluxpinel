# lupe_cruby.rb -- kernel/lupe.rb under CRuby.
#
#   ruby -r ./kernel/lupe_cruby.rb app.rb
#
# native_func gives each function of lupe.rb the CRuby code that does what
# Lupe does in the host build, so the same program runs under CRuby. Each
# load and eval has locals of its own, with main as self.

module LupeCRuby
  # a new binding at the top level
  def self.fresh = TOPLEVEL_BINDING.eval("proc { binding }").call

  FUNCS = {
    "lupe_api_load" => ->(path) { LupeCRuby.fresh.eval(File.read(path), path) },
    "lupe_api_eval" => ->(src) { LupeCRuby.fresh.eval(src) },
    "lupe_api_call" => ->(obj, name, args) { obj.public_send(name, *args) }
  }.freeze
end

class Module
  def native_func(name, _args, _ret, cname)
    define_singleton_method(name, &LupeCRuby::FUNCS.fetch(cname))
  end
end
