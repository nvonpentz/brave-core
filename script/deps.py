#!/usr/bin/env python
# Copyright (c) 2012 The Chromium Authors. All rights reserved.
# Use of this source code is governed by a BSD-style license that can be
# found in the LICENSE file.
"""This script is used to download deps."""

import os
import sys
import tarfile
import tempfile
import time
import zipfile

try:
    from urllib2 import HTTPError, URLError, urlopen
except ImportError:  # For Py3 compatibility
    from urllib.error import HTTPError, URLError
    from urllib.request import urlopen

ETAG_FILENAME = '.brave_deps_etag'

def DownloadUrl(url, output_file, old_etag):
    """Download url into output_file."""
    CHUNK_SIZE = 4096
    TOTAL_DOTS = 10
    num_retries = 3
    retry_wait_s = 5  # Doubled at each retry.

    while True:
        try:
            sys.stdout.write('Downloading %s ' % url)
            sys.stdout.flush()
            response = urlopen(url)
            total_size = int(response.info().get('Content-Length').strip())
            new_etag = response.info().get('etag')
            if old_etag and old_etag == new_etag:
                print(' ETag is the same, skipping download.')
                return old_etag
            bytes_done = 0
            dots_printed = 0
            while True:
                chunk = response.read(CHUNK_SIZE)
                if not chunk:
                    break
                output_file.write(chunk)
                bytes_done += len(chunk)
                num_dots = TOTAL_DOTS * bytes_done / total_size
                sys.stdout.write('.' * int(num_dots - dots_printed))
                sys.stdout.flush()
                dots_printed = num_dots
            if bytes_done != total_size:
                raise URLError("only got %d of %d bytes" %
                               (bytes_done, total_size))
            print(' Done.')
            return new_etag
        except URLError as e:
            sys.stdout.write('\n')
            print(e)
            if num_retries == 0 or isinstance(e, HTTPError) and e.code == 404:
                raise e
            num_retries -= 1
            print('Retrying in %d s ...' % retry_wait_s)
            time.sleep(retry_wait_s)
            retry_wait_s *= 2


def EnsureDirExists(path):
    if not os.path.exists(path):
        os.makedirs(path)


def ReadETag(output_dir):
    etag_filename = os.path.join(output_dir, ETAG_FILENAME)
    if not os.path.exists(etag_filename):
        return
    try:
        with open(etag_filename, 'r') as f:
            return f.read()
    except IOError as e:
        print(f'Failed to read etag from {output_dir}: {e}')
        return


def WriteETag(output_dir, etag):
    try:
        with open(os.path.join(output_dir, ETAG_FILENAME), 'w') as f:
            return f.write(etag)
    except IOError as e:
        print(f'Failed to write etag to {output_dir}: {e}')


def DownloadAndUnpack(url, output_dir, path_prefix=None):
    """Download an archive from url and extract into output_dir. If path_prefix is not
        None, only extract files whose paths within the archive start with path_prefix."""
    with tempfile.TemporaryFile() as f:
        old_etag = ReadETag(output_dir)
        new_etag = DownloadUrl(url, f, old_etag)
        if old_etag and old_etag == new_etag:
            return
        EnsureDirExists(output_dir)
        f.seek(0)
        if url.endswith('.zip'):
            assert path_prefix is None
            zipfile.ZipFile(f).extractall(path=output_dir)
        else:
            t = tarfile.open(mode='r:gz', fileobj=f)
            members = None
            if path_prefix is not None:
                members = [
                    m for m in t.getmembers() if m.name.startswith(path_prefix)
                ]
            t.extractall(path=output_dir, members=members)
            if new_etag:
                WriteETag(output_dir, new_etag)


if __name__ == '__main__':
    sys.exit(main())
