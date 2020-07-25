#!/usr/bin/env python
#
# Copyright 2019 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.

import os
import sys

import difflib
from util import build_utils


def _SkipOmitted(line):
  """
  Skip lines that are to be intentionally omitted from the expectations file.

  This is required when the file to be compared against expectations contains
  a line that changes from build to build because - for instance - it contains
  version information.
  """
  if line.endswith('# OMIT FROM EXPECTATIONS\n'):
    return '# THIS LINE WAS OMITTED\n'
  return line


def _GenerateDiffWithOnlyAdditons(expected_path, actual_data):
  """Generate a diff that only contains additions"""
  # Ignore blank lines when creating the diff to cut down on whitespace-only
  # lines in the diff.
  with open(expected_path) as expected:
    expected_lines = [l for l in expected.readlines() if l.strip()]
  actual_lines = [l for l in actual_data.splitlines(True) if l.strip()]

  diff = difflib.ndiff(expected_lines, actual_lines)
  filtered_diff = (line for line in diff if line.startswith('+'))
  return ''.join(filtered_diff)


def _DiffFileContents(expected_path, actual_data):
  """Check file contents for equality and return the diff or None."""
  with open(expected_path) as f_expected:
    expected_lines = f_expected.readlines()
  actual_lines = [_SkipOmitted(line) for line in actual_data.splitlines(True)]

  if expected_lines == actual_lines:
    return None

  expected_path = os.path.relpath(expected_path, build_utils.DIR_SOURCE_ROOT)

  diff = difflib.unified_diff(
      expected_lines,
      actual_lines,
      fromfile=os.path.join('before', expected_path),
      tofile=os.path.join('after', expected_path),
      n=0)

  return ''.join(diff).rstrip()


def AddCommandLineFlags(parser):
  group = parser.add_argument_group('Expectations')
  group.add_argument(
      '--expected-file',
      help='Expected contents for the check. If --expected-file-base  is set, '
      'this is a diff of --actual-file and --expected-file-base.')
  group.add_argument(
      '--expected-file-base',
      help='File to diff against before comparing to --expected-file.')
  group.add_argument('--actual-file',
                     help='Path to write actual file (for reference).')
  group.add_argument('--failure-file',
                     help='Write to this file if expectations fail.')
  group.add_argument('--fail-on-expectations',
                     action="store_true",
                     help='Fail on expectation mismatches.')
  group.add_argument('--only-verify-expectations',
                     action='store_true',
                     help='Verify the expectation and exit.')


def CheckExpectations(actual_data, options):
  with build_utils.AtomicOutput(options.actual_file) as f:
    f.write(actual_data)
  if options.expected_file_base:
    actual_data = _GenerateDiffWithOnlyAdditons(options.expected_file_base,
                                                actual_data)
  diff_text = _DiffFileContents(options.expected_file, actual_data)

  if not diff_text:
    return

  fail_msg = """
Expectations need updating:
https://chromium.googlesource.com/chromium/src/+/HEAD/chrome/android/expecations/README.md

LogDog tip: Use "Raw log" or "Switch to lite mode" before copying:
https://bugs.chromium.org/p/chromium/issues/detail?id=984616

To update expectations, run:
########### START ###########
 patch -p1 <<'END_DIFF'
{}
END_DIFF
############ END ############
""".format(diff_text)

  sys.stderr.write(fail_msg)
  if options.failure_file:
    build_utils.MakeDirectory(os.path.dirname(options.failure_file))
    with open(options.failure_file, 'w') as f:
      f.write(fail_msg)
  if options.fail_on_expectations:
    sys.exit(1)
