define print_array

  if $argc < 1
    echo print_array ARRAY [NUM] [FIELD]\n
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
Syntax: print_array ARRAY [NUM] [FIELD]

Print array's content.

ARRAY is the array itself. If the optional argument NUM
is given, the first NUM entries will be displayed. If
addionally FIELD is given, only this field of the arrays
structure is printed.
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

### Local Variables:
### mode: gdb-script
### End:
