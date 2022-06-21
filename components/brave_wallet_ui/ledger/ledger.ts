/* Copyright (c) 2022 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { LedgerCommand, kLedgerBridgeUrl } from '../common/hardware/ledgerjs/ledger-messages'
import { LedgerUntrustedMessagingTransport } from '../common/hardware/ledgerjs/ledger-untrusted-transport'

const untrustedMessagingTransport = new LedgerUntrustedMessagingTransport(window.parent, 'chrome://wallet')

// Set up event listener for authorize button
let authorizeBtn
window.addEventListener('DOMContentLoaded', (event) => {
  authorizeBtn = document.getElementById('authorize')
  if (authorizeBtn) {
    authorizeBtn.addEventListener('click', () => {
      untrustedMessagingTransport.promptAuthorization().then((result) => {
        untrustedMessagingTransport.sendCommand({
          id: LedgerCommand.AuthorizationSuccess,
          origin: kLedgerBridgeUrl,
          command: LedgerCommand.AuthorizationSuccess
        })
      })
      // TODO handle authorize failure / deny, button disable
    })
  } 
})
