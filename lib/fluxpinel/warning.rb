# warning.rb -- the module Warning: the categories of warnings and
# Warning.warn.
#
# spinel has no Warning. Fluxpinel loads this file at the first read of the
# constant (src/global.c). A program turns a category on or off
# (`Warning[:deprecated] = true`, as minitest/autorun does) and reads it.
# Fluxpinel itself writes no warning of a category, so the values change
# nothing in its own output.
module Warning
  @categories = { deprecated: false, experimental: true, performance: false, strict_unused_block: false }

  def self.categories
    @categories.keys
  end

  def self.[](category)
    raise TypeError, "wrong argument type #{category.class} (expected Symbol)" unless category.is_a?(Symbol)
    raise ArgumentError, "unknown category: #{category}" unless @categories.key?(category)
    @categories[category]
  end

  def self.[]=(category, flag)
    raise TypeError, "wrong argument type #{category.class} (expected Symbol)" unless category.is_a?(Symbol)
    raise ArgumentError, "unknown category: #{category}" unless @categories.key?(category)
    @categories[category] = flag ? true : false
    flag
  end

  # Writes the message to $stderr. The category is checked and not used.
  def self.warn(message, category: nil)
    raise TypeError, "wrong argument type #{message.class} (expected String)" unless message.is_a?(String)
    self[category] unless category.nil?
    $stderr.write(message)
    nil
  end

  # Warning#warn, for a module that a program extends Warning with.
  def warn(message, category: nil)
    Warning.warn(message, category: category)
  end
end
