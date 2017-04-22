#!/bin/bash -ex
#
# Copyright 2016-2017, Intel Corporation
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
# run-build.sh - is called inside a Docker container;
#		starts a build of PMEMFILE project
#

# Build all and run tests
cd $WORKDIR
mkdir /tmp/pmemfile-tests
if [ -n "$COMPILER" ]; then
	export CC=$COMPILER
fi
cp /googletest-1.8.0.zip .

mkdir build
cd build
cmake .. -DDEVELOPER_MODE=1 \
		-DCMAKE_INSTALL_PREFIX=/tmp/pmemfile \
		-DCMAKE_BUILD_TYPE=Debug \
		-DTEST_DIR=/tmp/pmemfile-tests \
		-DTRACE_TESTS=1 \
		-DTESTS_USE_FORCED_PMEM=1

make
ctest --output-on-failure
make install
cd ..
rm -r build


mkdir build
cd build
cmake .. -DCMAKE_INSTALL_PREFIX=/tmp/pmemfile \
		-DCMAKE_BUILD_TYPE=Release \
		-DTEST_DIR=/tmp/pmemfile-tests \
		-DTRACE_TESTS=1 \
		-DTESTS_USE_FORCED_PMEM=1

make
ctest --output-on-failure
make install
cd ..
rm -r build
