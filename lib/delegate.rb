# delegate.rb -- Delegator, SimpleDelegator and DelegateClass, in Ruby.
#
# spinel has no package of this name. The library of CRuby makes Delegator
# from BasicObject and a copy of Kernel with some methods taken out
# (Kernel.dup, undef_method), which Fluxpinel does not run. This file gives
# the same methods another way: Delegator is a class under Object, and it
# defines the methods that the copy of Kernel does not have (to_s, inspect,
# =~, !~, ===, <=>, hash) as calls of the target.
#
# The differences from CRuby: Delegator.superclass is Object, and a method
# that Object has and Kernel does not have is not a call of the target.

class Delegator
  def initialize(obj)
    __setobj__(obj)
  end

  # The target, or the value of the block when there is none.
  def __getobj__
    raise ::NotImplementedError, "need to define '__getobj__'"
  end

  def __setobj__(obj)
    raise ::NotImplementedError, "need to define '__setobj__'"
  end

  def method_missing(m, *args, &block)
    has = true
    target = __getobj__ { has = false }
    if has && target.respond_to?(m)
      target.__send__(m, *args, &block)
    else
      super(m, *args, &block)
    end
  end

  def respond_to_missing?(m, include_private)
    has = true
    target = __getobj__ { has = false }
    has && target.respond_to?(m, false)
  end

  # The methods that Kernel gives an object and that a delegator hands on.
  def to_s
    __getobj__.to_s
  end

  def inspect
    __getobj__.inspect
  end

  def =~(other)
    __getobj__ =~ other
  end

  def !~(other)
    !(__getobj__ =~ other)
  end

  def ===(other)
    __getobj__ === other
  end

  def <=>(other)
    __getobj__ <=> other
  end

  def hash
    __getobj__.hash
  end

  def ==(obj)
    return true if obj.equal?(self)
    __getobj__ == obj
  end

  def !=(obj)
    return false if obj.equal?(self)
    __getobj__ != obj
  end

  def eql?(obj)
    return true if obj.equal?(self)
    obj.eql?(__getobj__)
  end

  def !
    !__getobj__
  end

  def methods(all = true)
    __getobj__.methods(all) | super
  end

  def public_methods(all = true)
    __getobj__.public_methods(all) | super
  end

  def protected_methods(all = true)
    __getobj__.protected_methods(all) | super
  end

  def marshal_dump
    ivars = instance_variables.reject { |var| /\A@delegate_/.match?(var.to_s) }
    [:__v2__, ivars, ivars.map { |var| instance_variable_get(var) }, __getobj__]
  end

  def marshal_load(data)
    version, vars, values, obj = data
    if version == :__v2__
      vars.each_with_index { |var, i| instance_variable_set(var, values[i]) }
      __setobj__(obj)
    else
      __setobj__(data)
    end
  end

  def initialize_clone(obj, freeze: nil)
    __setobj__(obj.__getobj__.clone(freeze: freeze))
  end

  def initialize_dup(obj)
    __setobj__(obj.__getobj__.dup)
  end

  # Fluxpinel calls only initialize_copy for dup and for clone: the copy
  # gets a dup of the target.
  def initialize_copy(obj)
    __setobj__(obj.__getobj__.dup)
  end

  # Freezes the delegator and its target.
  def freeze
    __getobj__.freeze
    super()
  end

  # The public methods of a delegator class: its own and those of the
  # class that it stands for.
  def self.public_api
    public_instance_methods
  end
end

# A delegator whose target is an instance variable.
#
#   class Stats < SimpleDelegator
#     def average = sum / size.to_f
#   end
#   Stats.new([1, 2, 3]).average
class SimpleDelegator < Delegator
  def __getobj__
    unless defined?(@delegate_sd_obj)
      return yield if block_given?
      __raise__ ::ArgumentError, "not delegated"
    end
    @delegate_sd_obj
  end

  def __setobj__(obj)
    __raise__ ::ArgumentError, "cannot delegate to self" if equal?(obj)
    @delegate_sd_obj = obj
  end

  def __raise__(*args)
    ::Kernel.raise(*args)
  end
  private :__raise__
end

# A class of delegators for the instances of superclass: each public method
# of superclass is a method of the answer, so a call does not go through
# method_missing.
#
#   class Tempfile < DelegateClass(File)
def DelegateClass(superclass, &block)
  klass = Class.new(Delegator)
  kept = Delegator.instance_methods | Delegator.private_instance_methods
  kept -= [:to_s, :inspect, :=~, :!~, :===, :<=>, :hash]
  names = superclass.instance_methods - kept
  protected_names = superclass.protected_instance_methods
  klass.class_eval do
    def __getobj__
      unless defined?(@delegate_dc_obj)
        return yield if block_given?
        raise ::ArgumentError, "not delegated"
      end
      @delegate_dc_obj
    end

    def __setobj__(obj)
      raise ::ArgumentError, "cannot delegate to self" if equal?(obj)
      @delegate_dc_obj = obj
    end

    names.each do |name|
      define_method(name) do |*args, &blk|
        __getobj__.__send__(name, *args, &blk)
      end
      protected name if protected_names.include?(name)
    end
  end
  klass.define_singleton_method(:public_instance_methods) do |all = true|
    super(all) | superclass.public_instance_methods
  end
  klass.define_singleton_method(:protected_instance_methods) do |all = true|
    super(all) | superclass.protected_instance_methods
  end
  klass.class_eval(&block) if block
  klass
end
