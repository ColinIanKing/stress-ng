#!/bin/bash
#
# Copyright (C) 2012-2017 Canonical
#
# This program is free software; you can redistribute it and/or
# modify it under the terms of the GNU General Public License
# as published by the Free Software Foundation; either version 2
# of the License, or (at your option) any later version.
#
# This program is distributed in the hope that it will be useful,
# but WITHOUT ANY WARRANTY; without even the implied warranty of
# MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
# GNU General Public License for more details.
#
# You should have received a copy of the GNU General Public License
# along with this program; if not, write to the Free Software
# Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301, USA.
#
SMATCH_DIR=smatch
SMATCH_REPO=git://repo.or.cz/smatch
DEPENDENCIES="libxml2-dev llvm-dev libsqlite3-dev"

INCLUDES="-I/usr/lib/gcc/i686-linux-gnu/ -I/usr/include/x86_64-linux-gnu"

HERE=$(pwd)

#
#  Install any packages we depend on to build smatch
#
smatch_install_dependencies()
{
	install=""

	echo "Checking for dependencies for smatch.."

	for d in ${DEPENDENCIES}
	do
		if [ "$(dpkg -l | grep $d)" == "" ]; then
			install="$install $d"
		fi
	done
	if [ "$install" != "" ]; then
		echo "Need to install:$install"
		sudo apt-get install $install
		if [ $? -ne 0 ]; then
			echo "Installation of packages failed"
			exit 1
		fi
	fi
}

#
#  Get an up to date version of smatch
#
smatch_get()
{
	if [ -d ${SMATCH_DIR} ]; then
		echo "Getting latest version of smatch.."
		cd ${SMATCH_DIR}
		git checkout -f master >& /dev/null
		git fetch origin >& /dev/null
		git fetch origin master >& /dev/null
		git reset --hard FETCH_HEAD >& /dev/null
		cd ${HERE}
	else
		echo "Getting smatch.."
		git clone ${SMATCH_REPO} ${SMATCH_DIR}
	fi
}

#
#  Build smatch
#
smatch_build()
{
	cd ${SMATCH_DIR}
	echo "Smatch: make clean.."
	make clean >& /dev/null
	echo "Smatch: make.."
	make > /dev/null 2>&1
	if [ $? -eq 0 ]; then
		echo "Build of smatch succeeded"
	else
		echo "Build  of smatchfailed"
		exit 1
	fi
	cd ${HERE}
}

#
#  Build using smatch
#
smatch_check()
{
	echo "Smatchifying.."
	autoreconf -ivf > /dev/null
	./configure > /dev/null
	make clean

	make CHECK="${HERE}/${SMATCH_DIR}/smatch --full-path --two-passes" \
		CC="${HERE}/${SMATCH_DIR}/cgcc $INCLUDES" | tee smatch.log
}

#
#  Check for errors
#
smatch_errors()
{
	errors=$(grep "error: " smatch.log | wc -l)
	echo " "
	echo "Smatch found $errors errors, see smatch.log for more details."
}

smatch_install_dependencies
smatch_get
smatch_build
smatch_check
smatch_errors
