define print_array

  if $argc < 1
    echo print_array ARRAY [NUM [FIELD]]\n
  else
    set $array = $arg0
    if $argc < 2 
      set $num = 5
    else
      set $num = $arg1
    end
    set $i = 0

    while $i < $num
      output $i
      echo \ :\ \ 
      if $argc < 3
        output $array[$i++]
      else
        output $array[$i++].$arg2
      end
      echo \n
    end
  end
end


document print_array
Syntax: print_array ARRAY [NUM [FIELD]]

Print array's content.

ARRAY is the array itself. If the optional argument NUM is given, the
first NUM entries will be displayed. If furthermore FIELD is given, only
this field of the arrays structure is printed.
end

define print_list

  if $argc < 1
    echo print_list LISTHEAD [TYPE [NUM]]\n
  else
    set $lp = &$arg0
    if $argc < 3
      set $num = 1
    else
      set $num = $arg2
    end
    set $i = $num

    while $i > 0
      set $lp = $lp->next
      set $i = $i - 1

      if $lp == &$arg0
	loop_break
      end

      output $num - $i
      echo \ :\ \ 
      if $argc < 2
	output *((PStask_t *)((char *)($lp)-(unsigned long)(&((PStask_t *)0)->next)))
      else
	output *(($arg1 *)((char *)($lp)-(unsigned long)(&(($arg1 *)0)->next)))
      end
      echo \n
    end
  end
end

document print_list
Syntax: print_list LISTHEAD [TYPE [NUM]]

Print list defined with the help of the Linux kernel's list.h.

LISTHEAD is the corresponding anchor of the list. It is assumed, that
each entry of the list is of type TYPE. If TYPE is not given explicitely,
PStask_t is the assumed type. If the optional argument NUM is given, the
first NUM entries will be displayed. Otherwise only the first entry will
be printed.
end

define list_len

  if $argc < 1
    echo list_len LISTHEAD\n
  else
    set $lp = &$arg0
    set $i = 0

    while 1
      set $lp = $lp->next

      if $lp == &$arg0
        loop_break
      end

      set $i = $i + 1

      if ($i % 10000) == 0
        output $i
        echo \n
      end

    end
  end
  output $i
  echo \n
end

document list_len
Syntax: list_len LISTHEAD

Print length of list defined with the help of the Linux kernel's list.h.

LISTHEAD is the corresponding anchor of the list.
end

define array_list_len

  if $argc < 1
    echo array_list_len ARRAY [FIELD [NUM [FIRST]]]\n
  else
    set $array = $arg0
    if $argc < 3
      set $num = 5
    else
      set $num = $arg2
    end
    if $argc < 4
      set $first = 0
    else
      set $first = $arg3
    end

    set $j = $first

    while $j < $num
      output $j
      echo \ :\ \ 
      if $argc < 2
        list_len $array[$j].list
      else
        list_len $array[$j].$arg1
      end
      set $j = $j + 1
    end
  end
end

document array_list_len
Syntax: array_list_len ARRAY [FIELD [NUM [FIRST]]]

Print length of lists that are arranged as array-members.

ARRAY is the name of the array to look at. Each array entry is assumed
to hold a list within the member FIELD. If FIELD is not given explicitely,
'list' is the assumed member-name. If the optional argument NUM is given,
the first NUM entries will be displayed. Otherwise only the first five
entries will be printed. If furthermore FIRST is given, handling ARRAY
will not start at the first element but at the number given.
end



### Local Variables:
### mode: gdb-script
### End:
