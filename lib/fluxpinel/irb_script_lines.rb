# irb_script_lines.rb -- the source of a method of a session, for the
# show_source command of IRB.
#
# IRB reads the file of a method for its source. A method of the session
# has no file ("(irb)"), and IRB then asks CRuby for the lines of the
# eval: RubyVM::InstructionSequence.of(method).script_lines. Fluxpinel
# keeps the String of each eval, and answers the same call from it. This
# file is all of RubyVM that Fluxpinel has, and only a session of IRB
# loads it: a program that asks defined?(RubyVM) gets no answer.
#
# `module RubyVM` is refused, as the reopening of a builtin module that
# Fluxpinel does not have, so const_set makes the module.

unless defined?(RubyVM)
  sequence = Class.new do
    # The sequence of a Method, an UnboundMethod or a Proc of the program,
    # or nil for a builtin one.
    def self.of(body)
      lines = body.__fx_script_lines__
      lines && new(lines)
    rescue NoMethodError
      nil   # no Method and no Proc
    end

    attr_reader :script_lines

    def initialize(script_lines)
      @script_lines = script_lines
    end
  end
  Object.const_set(:RubyVM, Module.new).const_set(:InstructionSequence, sequence)
end
