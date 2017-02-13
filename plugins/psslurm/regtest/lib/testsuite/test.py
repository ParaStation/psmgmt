
import traceback
import sys


_RETVAL = 0

#
# Mark test as failed.
def fail():
	global _RETVAL

	_RETVAL = 1

#
# End of each test
def quit():
	global _RETVAL

	if 0 == _RETVAL:
		sys.stdout.write(" sys.exit(0)")
	else:
		sys.stdout.write(" sys.exit(%d)" % _RETVAL)

	sys.exit(_RETVAL)

#
# Report the outcome of a check to stdout or stderr (depending on the
# outcome).
def _check_report_outcome(fo, prefix, msg, stack):
	if msg:
		prefix += " ('%s'):\n" % msg
	else:
		prefix += ":\n"

	fo.write(prefix)
	map(lambda x: fo.write("\t" + x.strip() + "\n"), stack)

#
# Check that condition x evaluates to True.
def check(x, msg = None):
	stack = traceback.format_stack()

	if x:
		_check_report_outcome(sys.stdout, "Test success", msg, stack)
	else:
		fail()
		_check_report_outcome(sys.stderr, "Test failure", msg, stack)

