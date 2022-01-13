/* Copyright (c) 2022 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_CHROMIUM_SRC_COURGETTE_THIRD_PARTY_BSDIFF_PAGED_ARRAY_H_
#define BRAVE_CHROMIUM_SRC_COURGETTE_THIRD_PARTY_BSDIFF_PAGED_ARRAY_H_

namespace courgette {

// Chromium's courgette.exe runs into memory allocation errors when called on
// Brave's binaries. Reducing the log page size from 18 (=1 MB) to 14
// (=1/16th MB) fixes this. This makes it seem likely that the errors are
// caused by address space fragmentation. Unfortunately, we have not yet been
// able to find the root cause for this. So we use this workaround.
constexpr int kPagedArrayDefaultPageLogSize = 14;

}

#define kPagedArrayDefaultPageLogSize kPagedArrayDefaultPageLogSize_Unused

#include "src/courgette/third_party/bsdiff/paged_array.h"

#undef kPagedArrayDefaultPageLogSize

#endif  // BRAVE_CHROMIUM_SCOURGETTE_THIRD_PARTY_BSDIFF_PAGED_ARRAY_H_
