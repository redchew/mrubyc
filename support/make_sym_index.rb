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
# make tree index.
#
def make_tree( sym_index, i = 0 )
  node_right = sym_index.pop( sym_index.size / 2 )
  node = sym_index.pop
  node_left = sym_index

  # left side
  case node_left.size
  when 0
    # nothing to do
  when 1
    node[:left] = (i += 1)
  else
    node[:left] = i + 1
    node_left = make_tree( node_left, i + 1 )
    i += node_left.size
  end

  # right side
  case node_right.size
  when 0
    # nothing to do
  when 1
    node[:right] = (i += 1)
  else
    node[:right] = i + 1
    node_right = make_tree( node_right, i + 1 )
    i += node_right.size
  end

  return [node] + node_left + node_right
end


##
# search index
#
def search_index( sym_index, str )
  hash = calc_hash( str )

  i = 0
  nest = 0
  while true
    nest += 1
    if (sym_index[i][:hash] == hash) && (str == sym_index[i][:cstr])
      return i, nest
    end

    if hash < sym_index[i][:hash]
      i = sym_index[i][:left]
    else
      i = sym_index[i][:right]
    end

    break if i == 0
  end

  return -1;
end



##
# main
#
sym_index = []

all_symbols.each {|s|
  sym_index << {:hash=>calc_hash(s.to_s), :left=>0, :right=>0, :cstr=>s.to_s}
}
sym_index.sort_by! {|index| index[:hash] }

# check collision.
i = 1
while i < sym_index.size
  if sym_index[i-1][:hash] == sym_index[i][:hash]
    STDERR.puts "Hash collision detected. #{sym_index[i-1][:cstr].inspect} and #{sym_index[i][:cstr].inspect}"
    exit 1
  end
  i += 1
end

sym_index = make_tree( sym_index )

# check search success.
all_symbols.each {|s|
  i,n = search_index( sym_index, s.to_s )
  if i < 0
    STDERR.puts "Can't find #{s.inspect}"
    exit 1
  end
#  p [s,i,n]
}

puts "static const struct SYM_INDEX builtin_sym_index[] = {"
sym_index.each {|index|
  printf "  {0x%04x,", index[:hash]
  printf " 0x%02x,", index[:left]
  printf " 0x%02x,", index[:right]
  printf " \"%s\"},\n", index[:cstr]
}
puts "};"
