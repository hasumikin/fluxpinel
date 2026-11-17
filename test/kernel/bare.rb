# A kernel with no entry (make test-host): the build passes no --ext-entry,
# and a script calls the methods of the table of --ext-class only. CRuby
# requires kernel/fluxpinel_cruby.rb and this file for the answer of
# test/host_bare.

require_relative "../../kernel/fluxpinel"

module Bare
  class Tally
    attr_reader :n

    def initialize(n) = @n = n

    def add(k) = @n += k
  end

  def self.fib(n) = n < 2 ? n : fib(n - 1) + fib(n - 2)

  def self.tally(n) = Tally.new(n)
end
