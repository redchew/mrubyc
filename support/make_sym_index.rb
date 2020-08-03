#!/usr/bin/env ruby
#
# create built-in symbol table in ROM
#
# (output examples)
# static const struct SYM_INDEX base_index[] = {
#   {0xdf37, 0x02, 0x01, "Object"},
#   {0xe9ef, 0x00, 0x00, "Array"},
#   {0xdb54, 0x00, 0x00, "Hash"},
# };


all_symbols = [:Object, :new, :!, :!=, :<=>, :===, :class, :dup, :block_given?, :is_a?, :kind_of?, :nil?, :p, :print, :puts, :raise, :object_id, :instance_methods, :instance_variables, :memory_statistics, :attr_reader, :attr_accessor, :sprintf, :printf, :inspect, :to_s, :Proc, :call, :NilClass, :to_i, :to_a, :to_h, :to_f, :TrueClass, :FalseClass, :Symbol, :all_symbols, :id2name, :to_sym, :Fixnum, :[], :+@, :-@, :**, :%, :&, :|, :^, :~, :<<, :>>, :abs, :chr, :Float, :String, :+, :*, :size, :length, :[]=, :b, :clear, :chomp, :chomp!, :empty?, :getbyte, :index, :ord, :slice!, :split, :lstrip, :lstrip!, :rstrip, :rstrip!, :strip, :strip!, :intern, :tr, :tr!, :start_with?, :end_with?, :include?, :Array, :at, :delete_at, :count, :first, :last, :push, :pop, :shift, :unshift, :min, :max, :minmax, :join, :Range, :exclude_end?, :Hash, :delete, :has_key?, :has_value?, :key, :keys, :merge, :merge!, :values, :Exception, :message, :StandardError, :RuntimeError, :ZeroDivisionError, :ArgumentError, :IndexError, :TypeError, :collect, :map, :collect!, :map!, :delete_if, :each, :each_index, :each_with_index, :reject!, :reject, :sort!, :sort, :RUBY_VERSION, :MRUBYC_VERSION, :times, :loop, :each_byte, :each_char]

all_symbols << :initialize


##
# Calculate hash value.
#
# (note)
#  Must have the same algorithm as the calc_hash function in symbol.c
#
def calc_hash( str )
  h = 0

  str.each_byte {|b|
    h = (h * 17 + b) & 0xffff
  }

  return h
end


##
# Add index table
#
def add_index( sym_index, str )
  hash = calc_hash(str)

  # check collision
  sym_index.each {|index|
    if hash == index[:hash]
      STDERR.puts "Hash collision detected."
      exit
    end
  }

  index = { :hash=>hash, :left=>0, :right=>0, :cstr=>str }

  # walk tree and add this.
  if !sym_index.empty?
    i = 0
    while true
      if hash < sym_index[i][:hash]
        # left side
        if sym_index[i][:left] == 0       # left is empty?
          sym_index[i][:left] = sym_index.size
          break
        end
        i = sym_index[i][:left]
      else
        # right side
        if sym_index[i][:right] == 0      # right is empty?
          sym_index[i][:right] = sym_index.size
          break
        end
        i = sym_index[i][:right]
      end
    end
  end

  sym_index << index
end


##
# main
#
sym_index = []
all_symbols.each {|sym|
  add_index( sym_index, sym.to_s )
}

puts "static const struct SYM_INDEX base_index[] = {"
sym_index.each {|index|
  printf "  {0x%04x,", index[:hash]
  printf " 0x%02x", index[:left]
  printf " 0x%02x", index[:right]
  printf " \"%s\"},\n", index[:cstr]
}
puts "};"
