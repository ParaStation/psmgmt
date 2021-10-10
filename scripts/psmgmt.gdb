#
# ParaStation
#
# Copyright (C) 2011-2021 ParTec Cluster Competence Center GmbH, Munich
# Copyright (C) 2021 ParTec AG, Munich
#
# This file may be distributed under the terms of the Q Public License
# as defined in the file LICENSE.QPL included in the packaging of this
# file.
#
# Author:      Norbert Eicker <eicker@par-tec.com>
#

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
Syntax: print_array ARRAY [NUM [ENTRY]]

Print array's content.

ARRAY is the array itself. If the optional argument NUM is given, the
first NUM elements will be displayed. If furthermore ENTRY is given,
the array is assumed to have structured elements and only this entry
of the structure is printed.

end


define reverse_print_list

  if $argc < 1
    echo reverse_print_list LISTHEAD [TYPE [NUM]]\n
  else
    set $lp = &$arg0
    if $argc < 3
      set $num = 1
    else
      set $num = $arg2
    end
    set $i = $num

    while $i > 0
      set $lp = $lp->prev
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

document reverse_print_list
Syntax: reverse_print_list LISTHEAD [TYPE [NUM]]

Print list defined with the help of the Linux kernel's list.h.

LISTHEAD is the corresponding anchor of the list. It is assumed, that
each element of the list is of type TYPE. If TYPE is not given
explicitely, PStask_t is the assumed type. If the optional argument
NUM is given, the first NUM elements will be displayed. Otherwise only
the first element will be printed.

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
each element of the list is of type TYPE. If TYPE is not given
explicitely, PStask_t is the assumed type. If the optional argument
NUM is given, the first NUM elements will be displayed. Otherwise only
the first element will be printed.

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
Syntax: array_list_len ARRAY [ENTRY [NUM [FIRST]]]

Print length of lists that are arranged as members of structured array
elements.

ARRAY is the name of the array to look at. Each array element is
assumed to be structured and to contain a list within the entry named
ENTRY. If ENTRY is not given explicitely, 'list' is the assumed
entry-name. If the optional argument NUM is given, the first NUM array
elements will be displayed. Otherwise only the first five elements
will be printed. If furthermore FIRST is given, handling ARRAY will
not start at the first element but at the number given.

end


define print_msg_list

  if $argc < 1
    echo print_msg_list LISTHEAD [NUM]\n
  else
    set $lp = &$arg0
    if $argc < 2
      set $num = 1
    else
      set $num = $arg1
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
      output *(DDMsg_t *)((PSIDmsgbuf_t *)((char *)($lp)-(unsigned long)(&((PSIDmsgbuf_t *)0)->next)))->msg
      echo \n
    end
  end
end

document print_msg_list
Syntax: print_msg_list LISTHEAD [NUM]

Print list of messages.

LISTHEAD is the corresponding anchor of the list. It is assumed, that
each element of the list is of type PSIDmsgbuf_t. If the optional argument
NUM is given, the first NUM elements will be displayed. Otherwise only
the first element of the list will be printed.
end


define reverse_print_list_entry

  if $argc < 3
    echo reverse_print_list_entry LISTHEAD TYPE ENTRY [NUM]]\n
  else
    set $lp = &$arg0
    if $argc < 4
      set $num = 1
    else
      set $num = $arg3
    end
    set $i = $num

    while $i > 0
      set $lp = $lp->prev
      set $i = $i - 1

      if $lp == &$arg0
	loop_break
      end

      output $num - $i
      echo \ :\ \ 
      output (($arg1 *)((char *)($lp)-(unsigned long)(&(($arg1 *)0)->next)))->$arg2
      echo \n
    end
  end
end


document reverse_print_list_entry
Syntax: reverse_print_list_entry LISTHEAD TYPE ENTRY [NUM]

Print specific entries of each list element.

LISTHEAD is the corresponding anchor of the list. It is assumed, that
each element of the list is structured and of type TYPE. For each
list-element the entry named ENTRY will be printed. If the optional
argument NUM is given, the first NUM entries will be
displayed. Otherwise only the first entry of the list will be printed.

end


define print_list_entry

  if $argc < 3
    echo print_list_entry LISTHEAD TYPE ENTRY [NUM]]\n
  else
    set $lp = &$arg0
    if $argc < 4
      set $num = 1
    else
      set $num = $arg3
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
      output (($arg1 *)((char *)($lp)-(unsigned long)(&(($arg1 *)0)->next)))->$arg2
      echo \n
    end
  end
end


document print_list_entry
Syntax: print_list_entry LISTHEAD TYPE ENTRY [NUM]

Print specific entries of each list element.

LISTHEAD is the corresponding anchor of the list. It is assumed, that
each element of the list is structured and of type TYPE. For each
list-element the entry named ENTRY will be printed. If the optional
argument NUM is given, the first NUM entries will be
displayed. Otherwise only the first entry of the list will be printed.

end

define list_item

  if $argc < 2
    echo list_item LISTPOINTER TYPE\n
  else
    echo ($arg1\ *)
    output ((char *)($arg0)-(unsigned long)(&(($arg1 *)0)->next))
  end
end

document list_item
Syntax: list_item LISTPOINTER TYPE

LISTPOINTER points to the list anchor of the element. It is assumed that the
element is of type TYPE. A pointer to the actual element is returned.

### Local Variables:
### mode: gdb-script
### End:
