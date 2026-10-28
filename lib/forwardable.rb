# forwardable.rb -- Forwardable and SingleForwardable, in Ruby.
#
# spinel writes the forwarding methods of def_delegator and its kin at
# compile time, and its package is a stub. Fluxpinel defines them when the
# program calls the declarations. An accessor names an instance variable
# (:@a), a method (:items), a constant (Reline), self, or a chain of them
# ("@a.first"). The target is read at each call.

module Forwardable
  def self.__fx_target(obj, accessor)
    first, *rest = accessor.to_s.split(".")
    v = if first == "self"
          obj
        elsif first.start_with?("@")
          obj.instance_variable_get(first)
        elsif first.match?(/\A[A-Z]/)
          Object.const_get(first)
        else
          obj.__send__(first)
        end
    rest.each { |m| v = v.__send__(m) }
    v
  end

  def self.__fx_names(methods)
    (methods.is_a?(Array) ? methods : [methods]).reject do |m|
      m.to_s == "__send__" || m.to_s == "__id__"
    end
  end

  def instance_delegate(hash)
    hash.each do |methods, accessor|
      Forwardable.__fx_names(methods).each { |m| def_instance_delegator(accessor, m) }
    end
  end
  alias delegate instance_delegate

  def def_instance_delegators(accessor, *methods)
    Forwardable.__fx_names(methods).each { |m| def_instance_delegator(accessor, m) }
    methods
  end
  alias def_delegators def_instance_delegators

  def def_instance_delegator(accessor, method, ali = method)
    method = method.to_sym
    ali = ali.to_sym
    define_method(ali) do |*args, **kw, &blk|
      Forwardable.__fx_target(self, accessor).__send__(method, *args, **kw, &blk)
    end
    ali
  end
  alias def_delegator def_instance_delegator
end

module SingleForwardable
  def single_delegate(hash)
    hash.each do |methods, accessor|
      Forwardable.__fx_names(methods).each { |m| def_single_delegator(accessor, m) }
    end
  end
  alias delegate single_delegate

  def def_single_delegators(accessor, *methods)
    Forwardable.__fx_names(methods).each { |m| def_single_delegator(accessor, m) }
    methods
  end
  alias def_delegators def_single_delegators

  def def_single_delegator(accessor, method, ali = method)
    method = method.to_sym
    ali = ali.to_sym
    define_singleton_method(ali) do |*args, **kw, &blk|
      Forwardable.__fx_target(self, accessor).__send__(method, *args, **kw, &blk)
    end
    ali
  end
  alias def_delegator def_single_delegator
end
