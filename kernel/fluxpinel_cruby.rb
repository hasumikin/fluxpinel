# fluxpinel_cruby.rb -- kernel/fluxpinel.rb under CRuby.
#
#   ruby -r ./kernel/fluxpinel_cruby.rb app.rb
#
# native_func gives each function of fluxpinel.rb the CRuby code that does what
# Fluxpinel does in the host build, so the same program runs under CRuby. Each
# load and eval has locals of its own, with main as self.

module FluxpinelCRuby
  # a new binding at the top level
  def self.fresh = TOPLEVEL_BINDING.eval("proc { binding }").call

  FUNCS = {
    "fx_api_load" => ->(path) { FluxpinelCRuby.fresh.eval(File.read(path), path) },
    "fx_api_eval" => ->(src) { FluxpinelCRuby.fresh.eval(src) },
    "fx_api_call" => ->(obj, name, args) { obj.public_send(name, *args) }
  }.freeze
end

class Module
  def native_func(name, _args, _ret, cname)
    define_singleton_method(name, &FluxpinelCRuby::FUNCS.fetch(cname))
  end
end
