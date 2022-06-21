/* Copyright (c) 2021 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import * as React from 'react'

export const AuthorizeHardwareDeviceIFrame = () => {
  return (
    <iframe src="chrome-untrusted://ledger-bridge" allow="hid"/>
  )
}

export default AuthorizeHardwareDeviceIFrame
