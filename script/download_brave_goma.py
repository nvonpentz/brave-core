#!/usr/bin/env python3
# Copyright (c) 2022 The Brave Authors. All rights reserved.
# This Source Code Form is subject to the terms of the Mozilla Public
# License, v. 2.0. If a copy of the MPL was not distributed with this file,
# You can obtain one at https://mozilla.org/MPL/2.0/. */
"""Script to download Brave goma client."""

import os
import platform
import sys

import deps
from urllib.error import URLError

SRC_ROOT = os.path.abspath(
    os.path.join(os.path.dirname(__file__), os.pardir, os.pardir))

GOMA_DIR = os.path.join(SRC_ROOT, 'build', 'goma')


def get_brave_goma_client_url():
    # MacOS only for now.
    if platform.system() == 'Darwin':
        if platform.machine() == 'x86_64':
            arch = 'x64'
        elif platform.machine() == 'arm64':
            arch = 'arm64'
        else:
            print(
                'Unknown CPU architecture, skipping brave goma client download'
            )
            return None
        return f'https://brave-jenkins-build-artifacts.s3.us-west-2.amazonaws.com/goma-client/goma-client-mac-{arch}.tar.gz'


def main():
    goma_url = get_brave_goma_client_url()
    if not goma_url:
        return 0
    try:
        deps.DownloadAndUnpack(goma_url, GOMA_DIR)
    except URLError as e:
        print(f'Failed to download Brave goma: {e}')
        sys.exit(1)

    return 0


if __name__ == '__main__':
    sys.exit(main())
