# calc -- evaluates each argument as Ruby. Spinel compiles this file as the
# kernel of the host build, and Fluxpinel evaluates the arguments at run
# time. spinel alone refuses `eval` of a String that the program gets at run
# time; Fluxpinel.eval is a call of a C function, which spinel compiles.

require_relative "../../kernel/fluxpinel"

module Calc
  #: (Array[String]) -> nil
  def self.main(args)
    args.each do |src|
      puts "#{src} => #{Fluxpinel.eval(src).inspect}"
    end
    nil
  end
end
