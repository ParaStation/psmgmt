#! /bin/bash
#
# Copyright (C) 2014, 2015 ParTec Cluster Competence Center GmbH, Munich
#
# Author:       Thomas Moschny <moschny@par-tec.com>
#
# Expand or update $Id$-style keywords in a Git working directory for
# all files for which the 'rcs-keywords' attribute is set (see also
# gitattributes(5)).
#
# Differences from SVN:
#
# - Version numbers use this format: <tag>-<distance>-g<hash> instead
#   of a single integer, where <distance> is the number of commits
#   since the commit pointed to by <tag>.
#
# - The longdate in $Date$ is (intentionally) not localized (in SVN,
#   the latter part of the format string (the part in parens) is
#   localized, which we can't rally mimic here).
#
# - Unsupported: $URL$, $HeadURL$
#
# Example expansions for SVN, for reference:
#
# $Date: 2012-08-01 17:34:00 +0200 (Mi, 01. Aug 2012) $
# $Revision: 1 $
# $Rev: 1 $
# $LastChangedRevision: 1 $
# $Author: moschny $
# $LastChangedBy: moschny $
# $Id: testfile 1 2012-08-01 15:34:00Z moschny $
#

smudge() {
    filepath="$1"
    head="$2"

    [[ -z "${filepath}" || -z "${head}" ]] && return 1

    {
	IFS= read -rd '' commit
	IFS= read -rd '' author
	IFS= read -rd '' ts
    } < <(git log -n1 '--pretty=format:%H%x00%an%x00%at%n' "${head}" -- "${filepath}")

    [[ -z "${ts}" ]] && return 1

    longdate="$(LC_TIME=C date -d "@${ts}" +'%F %T %z (%a, %d %b %Y)')"
    shortdate="$(date --utc -d "@${ts}" +'%F %TZ')"
    describe="$(git describe --always --tags "${commit}")"
    file="${filepath##*/}"

    sed -i "${filepath}" -r \
	-e 's@\$Date(:( *| +[^\$]+ +))?\$@\$Date: '"${longdate}"' \$@g' \
	-e 's@\$(Revision|Rev|LastChangedRevision)(:( *| +[^\$]+ +))?\$@\$\1: '"${describe}"' \$@g' \
	-e 's@\$(Author|LastChangedBy)(:( *| +[^\$]+ +))?\$@\$\1: '"${author}"' \$@g' \
	-e 's@\$Id(:( *| +[^\$]+ +))?\$@\$Id: '"${file} ${describe} ${shortdate} ${author}"' \$@g'
}

main() {
    cdup="$(git rev-parse --show-cdup)"
    head="$(git rev-parse HEAD)"

    (
	cd "./${cdup}"
	while IFS= read -r line; do
	    if [[ $line = *rcs-keywords:\ set ]] ; then
		file="${line%%: *}"
		#printf '%s\n' "$file"
		smudge "$file" "$head"
	    fi
	done < <(git ls-files | git check-attr rcs-keywords --stdin)
    )
}

main
