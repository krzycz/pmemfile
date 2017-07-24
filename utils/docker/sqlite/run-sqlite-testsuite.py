#!/usr/bin/env python3

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
# runs sqlite testsuite
#

from subprocess import check_output, STDOUT, TimeoutExpired, CalledProcessError
from os import path, environ, linesep
from sys import exit
import argparse


class SqliteTest:
    raw_suite_grep = r"cd {}; grep -rL run_test_suite | grep '\.test$' | grep -v 'mallocAll' | sort"
    timeout = 15 * 60
    pf_env = environ.copy()

    def __init__(self, sqlite_dir, pf_install_dir, pf_pool, mountpoint):
        self.binary_path = '{}/testfixture'.format(sqlite_dir)
        self.test_dir = '{}/test/'.format(sqlite_dir)
        self.mountpoint = mountpoint

        self.pf_env.update({
            'PMEM_IS_PMEM_FORCE': '1',
            'LD_PRELOAD': '{}/lib/pmemfile_debug/libpmemfile.so'.format(pf_install_dir),
            'PMEMFILE_POOLS': '{0}:{1}'.format(mountpoint, pf_pool)
        })

        self.verbose = False
        self.results = {}
        self.suite = []
        self.prepare_suite()
        self.test = ''

    def __str__(self):
        if self.test not in self.results:
            return '{} not run'.format(self.test)

        test_info = self.results[self.test]
        if all(result == 'PASSED' for result in test_info.values()):
            return "{0:<23} PASSED".format(self.test)
        else:
            return "{0:<23} {1:<20} {2:<3}".format(self.test,
                                                   test_info['result_pf'] +
                                                   ' on pmemfile',
                                                   test_info['result_local'] + ' on local filesystem')

    def prepare_suite(self):
        suite_grep = self.raw_suite_grep.format(self.test_dir)
        output = check_output(suite_grep, stderr=STDOUT, shell=True,
                              timeout=self.timeout)
        self.suite = output.decode('utf-8').splitlines()

    def run(self, test, is_pf=False):
        self.test = test
        if test not in self.results:
            self.results[test] = dict.fromkeys(
                ['result_pf', 'result_local'], 'not run')

        result_field = 'result_pf' if is_pf else 'result_local'
        output = ''
        try:
            output = self.exec_test(is_pf).decode('utf-8')
            self.results[test][result_field] = 'PASSED'
        except TimeoutExpired as e:
            output = e.output.decode('utf-8')
            self.results[test][result_field] = "TIMEOUT"
        except CalledProcessError as e:
            output = e.output.decode('utf-8')
            self.results[test][result_field] = 'FAILED'
        except Exception as e:
            print('Unexpected Error.')
            output = e
            self.results[test][result_field] = 'ERROR'
        finally:
            if self.results[test][result_field] != 'PASSED':
                self.LOG(output)

    def exec_test(self, is_pf):
        test_path = path.join(self.test_dir, self.test)
        exec_args = [self.binary_path, test_path]
        env = self.pf_env if is_pf else None
        return check_output(exec_args, timeout=self.timeout,
                            stderr=STDOUT, env=env, cwd=self.mountpoint)

    def LOG(self, output):
        if self.verbose:
            print(output)


def test_pmemfile_against_local_fs(sqlite, tests, fails_path, verbose=False):
    sqlite.verbose = verbose
    for test in tests:
        sqlite.run(test, is_pf=True)
        sqlite.run(test)
        print(sqlite)

    failed_pf_only = [test for test in tests
                      if sqlite.results[test]['result_pf'] != 'PASSED' and
                      sqlite.results[test]['result_local'] == 'PASSED']

    failed_local_only = [test for test in tests
                         if sqlite.results[test]['result_pf'] == 'PASSED' and
                         sqlite.results[test]['result_local'] != 'PASSED']

    failed_both = [test for test in tests
                   if sqlite.results[test]['result_pf'] != 'PASSED' and
                   sqlite.results[test]['result_local'] != 'PASSED']

    fails_count = len(failed_both) + len(failed_local_only) + \
        len(failed_pf_only)

    print("{0}SUMMARY{0}Out of {1} test cases {2} failed.".format(
        linesep, len(tests), fails_count))

    if failed_pf_only:
        print("{0}{1} failed only on pmemfile:".format(
            linesep, len(failed_pf_only)))
        for test in failed_pf_only:
            print(test)

    if failed_both:
        print("{0}{1} failed on pmemfile and local filesystem:".format(
            linesep, len(failed_both)))
        for test in failed_both:
            print(test)

    if failed_local_only:
        print("{0}{1} failed only on local filesystem:".format(
            linesep, len(failed_local_only)))
        for test in failed_local_only:
            print(test)

    print(linesep)
    compare_with_past_execution(failed_pf_only, failed_both, fails_path)


def compare_with_past_execution(fails_pf_only, fails_both, fails_path):
    if not path.exists(fails_path):
        print('No file {}. No past execution to check against.'.format(fails_path))
        exit(0)

    with open(fails_path, 'r') as f:
        past_fails = [fail.strip() for fail in f.readlines() if fail.strip()]

    if set(fails_pf_only) == set(past_fails):
        print('No new fails.')
        exit(0)

    new_fails = set(fails_pf_only) - set(past_fails)
    if new_fails:
        print('New fails introduced in this execution:')
        for fail in new_fails:
            print(fail)
        exit(1)

    removed_fails = set(past_fails) - (set(fails_pf_only) | set(fails_both))
    if removed_fails:
        print('Some tests are no longer failing since last execution. Current fails list:')
        for fail in sorted(fails_pf_only):
            print(fail)
        exit(1)


if __name__ == "__main__":
    parser = argparse.ArgumentParser(
        description="Run sqlite tests with pmemfile.")
    parser.add_argument("-p", "--pf-pool", required=True,
                        help="Path to pmemfile_pool.")
    parser.add_argument("-m", "--mountpoint", required=True,
                        help="Path to pmemfile mountpoint directory.")
    parser.add_argument("-s", "--sqlite", required=True,
                        help="Path to sqlite main directory.")
    parser.add_argument("-i", "--pf-install-dir", required=True,
                        help="Path to pmemfile repository.")
    parser.add_argument(
        "-t", "--tests", help="File with list of test cases (in separate lines).")
    parser.add_argument("-v", "--verbose", action='store_true',
                        help="Print outputs of tests that failed.")
    parser.add_argument(
        "-f", "--fails", default='failing_short_tests', help="Path to the file with past fails.")
    parser.add_argument(
        "--timeout", type=int, default=15 * 60, help="Timeout for single test in seconds (default: 15 minutes).")

    args = parser.parse_args()

    sqlite = SqliteTest(args.sqlite, args.pf_install_dir,
                        args.pf_pool, args.mountpoint)
    sqlite.timeout = args.timeout

    to_run = []
    if args.tests is not None:
        with open(args.tests, 'r') as f:
            to_run = [test.strip() for test in f.readlines()]
    else:
        to_run = sqlite.suite

    test_pmemfile_against_local_fs(sqlite, to_run, args.fails, args.verbose)
