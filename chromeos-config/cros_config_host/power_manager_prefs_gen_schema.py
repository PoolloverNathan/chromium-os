#!/usr/bin/env python3
# -*- coding: utf-8 -*-
# Copyright 2020 The Chromium OS Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""Transforms power_manager prefs/defaults into enforceable schema."""

import argparse
import os
import re
import sys

THIS_DIR = os.path.dirname(__file__)

PREF_DEF_FILE = os.path.join(THIS_DIR, "../../power_manager/common/power_constants.cc")
PREF_DEFAULTS_DIR = os.path.join(THIS_DIR, "../../power_manager/default_prefs")


def ParseArgs(argv):
    """Parse the available arguments.

    Invalid arguments or -h cause this function to print a message and exit.

    Args:
      argv: List of string arguments (excluding program name / argv[0])

    Returns:
      argparse.Namespace object containing the attributes.
    """
    parser = argparse.ArgumentParser(
        description="Generates power_manager prefs jsonschema"
    )
    parser.add_argument(
        "-o",
        "--output",
        type=str,
        help="Output file that will be generated by the transform",
    )
    return parser.parse_args(argv)


def Main(output):
    """Transforms power_manager prefs/defaults into jsonschema.

    Args:
      output: Output file that will be generated by the transform.
    """
    result_lines = [
        """powerd_prefs_default: &powerd_prefs_default
  description: For details, see https://chromium.googlesource.com/chromiumos/platform2/+/HEAD/power_manager/
  type: string

powerd_prefs: &powerd_prefs"""
    ]

    with open(PREF_DEF_FILE, "r") as defs_stream:
        defs_content = defs_stream.read()
        prefs = re.findall(
            r'const char .*Pref.. =[ |\n] *"(.*)";', defs_content, re.MULTILINE
        )
        for pref in prefs:
            default_pref_path = os.path.join(PREF_DEFAULTS_DIR, pref)
            pref_name = pref.replace("_", "-")
            if os.path.exists(default_pref_path):
                result_lines.append("  %s:" % pref_name)
                result_lines.append("    <<: *powerd_prefs_default")
                with open(default_pref_path, "r") as default_stream:
                    default = default_stream.read()
                    result_lines.append(
                        '    default: "%s"' % default.strip().replace("\n", " ")
                    )
            else:
                result_lines.append("  %s: *powerd_prefs_default" % pref_name)

    full_result = "\n".join(result_lines)
    if output:
        with open(output, "w") as output_stream:
            print(full_result, file=output_stream)
    else:
        print(full_result)


def main(argv=None):
    """Main program which parses args and runs

    Args:
      argv: List of command line arguments, if None uses sys.argv.
    """
    if argv is None:
        argv = sys.argv[1:]
    opts = ParseArgs(argv)
    Main(opts.output)


if __name__ == "__main__":
    sys.exit(main(sys.argv[1:]))
