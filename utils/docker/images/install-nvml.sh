#!/bin/bash -ex
#
# Copyright 2017, Intel Corporation
#
# Redistribution and use in source and binary forms, with or without
# modification, are permitted provided that the following conditions
# are met:
#
#     * Redistributions of source code must retain the above copyright
#       notice, this list of conditions and the following disclaimer.
#
#     * Redistributions in binary form must reproduce the above copyright
#       notice, this list of conditions and the following disclaimer in
#       the documentation and/or other materials provided with the
#       distribution.
#
#     * Neither the name of the copyright holder nor the names of its
#       contributors may be used to endorse or promote products derived
#       from this software without specific prior written permission.
#
# THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
# "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
# LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
# A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
# OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
# SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
# LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
# DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
# THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
# (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
# OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.

#
# install-nvml.sh - installs libpmem & libpmemobj
#

git clone https://github.com/pmem/nvml.git
cd nvml
git checkout 3fdd3f91bf55d93779d05a680726f08c810511dd
BUILD_PACKAGE_CHECK=n make $1 EXTRA_CFLAGS="-DUSE_VALGRIND"
if [ "$1" = "dpkg" ]; then
	sudo dpkg -i dpkg/libpmem_*.deb dpkg/libpmem-dev_*.deb
	sudo dpkg -i dpkg/libpmemobj_*.deb dpkg/libpmemobj-dev_*.deb
	# nvml-tools deps
	sudo dpkg -i dpkg/libpmemblk_*.deb dpkg/libpmemlog_*.deb dpkg/libpmempool_*.deb
	sudo dpkg -i dpkg/nvml-tools_*.deb
elif [ "$1" = "rpm" ]; then
	sudo rpm -i rpm/*/libpmem-*.rpm
	sudo rpm -i rpm/*/libpmemobj-*.rpm
	# nvml-tools deps, version specified to avoid installing devel packages
	sudo rpm -i rpm/*/libpmemblk-1.3*.rpm
	sudo rpm -i rpm/*/libpmemlog-1.3*.rpm
	sudo rpm -i rpm/*/libpmempool-1.3*.rpm
	sudo rpm -i rpm/*/nvml-tools-*.rpm
fi
cd ..
rm -rf nvml
