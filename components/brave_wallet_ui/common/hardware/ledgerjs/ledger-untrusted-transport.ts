/* Copyright (c) 2022 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import TransportWebHID from '@ledgerhq/hw-transport-webhid'
import Sol from '@ledgerhq/hw-app-solana'
import { LedgerMessagingTransport } from './ledger-messaging-transport'
import {
  LedgerCommand,
  UnlockCommand,
  LedgerResponse,
  UnlockResponsePayload,
  GetAccountCommand,
  GetAccountResponse,
  GetAccountResponsePayload,
  SignTransactionCommand,
  SignTransactionResponsePayload,
  SignTransactionResponse,
} from './ledger-messages'

// LedgerTrustedMessagingTransport is the messaging transport object
// for chrome-untrusted://ledger-bridge. It primarily handles postMessages
// coming from chrome://wallet or chrome://wallet-panel by making calls
// to Ledger libraries and replying with the results.
export class LedgerUntrustedMessagingTransport extends LedgerMessagingTransport {
  constructor(targetWindow: Window, targetUrl: string) {
    super(targetWindow, targetUrl)
    this.addCommandHandler(LedgerCommand.Unlock, this.handleUnlock)
    this.addCommandHandler(LedgerCommand.GetAccount, this.handleGetAccount)
    this.addCommandHandler(LedgerCommand.SignTransaction, this.handleSignTransaction)
  }

  handleUnlock = (command: UnlockCommand): Promise<UnlockResponsePayload> => {
    return new Promise(async (resolve) => {
      if ((await this.authorizationNeeded())) {
        const error = {
          success: false,
          error: 'unauthorized',
          code: 'unauthorized'
        }
        resolve(this.createUnlockResponse(command, false, error))
        return
      }
      resolve(this.createUnlockResponse(command, true))
      return
    })
  }

  handleGetAccount = (command: GetAccountCommand, source: Window): Promise<GetAccountResponsePayload> => {
    return new Promise(async (resolve) => {
      const transport = await TransportWebHID.create()
      const app = new Sol(transport)
      try {
        const result = await app.getAddress(command.path)
        const getAccountResponse: GetAccountResponse = {
          success: true,
          address: result.address
        }
        resolve(this.createGetAccountResponse(command, getAccountResponse))
      } catch(error) {
        resolve(this.createGetAccountResponse(command, undefined, error))
      } finally {
        await transport.close()
      }
    })
  }

  handleSignTransaction = (command: SignTransactionCommand, source: Window): Promise<SignTransactionResponsePayload> => {
    return new Promise(async (resolve) => {
      const transport = await TransportWebHID.create()
      const app = new Sol(transport)
      const result: SignTransactionResponse = await app.signTransaction(command.path, Buffer.from(command.rawTxBytes))
      resolve({ id: command.id, command: command.command, payload: result, origin: command.origin })
      await transport.close()
    })
  }

  promptAuthorization = () => {
    return new Promise<void>(async (resolve) => {
      if (await this.authorizationNeeded()) {
        const transport = await TransportWebHID.create()
        await transport.close()
      }
      resolve()
    })
  }
 
  private authorizationNeeded = async (): Promise<boolean> => {
    return (await TransportWebHID.list()).length === 0
  }

  private createUnlockResponse = (command: UnlockCommand, result: boolean, error?: any): UnlockResponsePayload => {
    const payload: LedgerResponse = (!result && error) ? error : { success: result }
    return { id: command.id, command: command.command, payload: payload, origin: command.origin }
  }

  private createGetAccountResponse = (command: GetAccountCommand, result?: GetAccountResponse, error?: any): GetAccountResponsePayload => {
    let payload: GetAccountResponse = (!result && error) ? error : result
    if (error) {
      payload.success = false
    } else {
      payload.success = true
    }
    return { id: command.id, command: command.command, payload: payload, origin: command.origin }
  }
}
