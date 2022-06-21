/* Copyright (c) 2022 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { LEDGER_HARDWARE_VENDOR } from 'gen/brave/components/brave_wallet/common/brave_wallet.mojom.m.js'
import { BraveWallet } from '../../../constants/types'
import { getLocale } from '../../../../common/locale'
import { LedgerSolanaKeyring } from '../interfaces'
import { HardwareVendor } from '../../api/hardware_keyrings'
import {
  GetAccountsHardwareOperationResult,
  HardwareOperationResult,
  SignHardwareOperationResult
} from '../types'
import {
  kLedgerBridgeUrl,
  LedgerCommand,
  UnlockResponsePayload,
  LedgerFrameCommand,
  GetAccountResponse,
  GetAccountResponsePayload,
  SignTransactionResponse,
  SignTransactionResponsePayload,
  LedgerErrorsCodes,
  LedgerError
} from './ledger-messages'
import { LedgerTrustedMessagingTransport } from './ledger-trusted-transport'
import { hardwareDeviceIdFromAddress } from '../hardwareDeviceIdFromAddress'

export default class SolanaLedgerBridgeKeyring implements LedgerSolanaKeyring {
  private deviceId: string
  private onAuthorized?: (showAuthorizeDevice: boolean) => void
  private transport?: LedgerTrustedMessagingTransport
  private bridge?: HTMLIFrameElement
  private readonly frameId: string

  constructor(onAuthorized?: (showAuthorizeDevice: boolean) => void) {
    this.onAuthorized = onAuthorized
    // @ts-expect-error
    this.frameId = crypto.randomUUID()
  }

  coin = (): BraveWallet.CoinType => {
    return BraveWallet.CoinType.SOL
  }

  type = (): HardwareVendor => {
    return LEDGER_HARDWARE_VENDOR
  }

  // TODO
  // cancelOperation = async () => {
  //   closeLedgerBridge()
  // }

  unlock = async (): Promise<HardwareOperationResult> => {
    const data = await this.sendCommand<UnlockResponsePayload>({
      id: LedgerCommand.Unlock,
      origin: window.origin,
      command: LedgerCommand.Unlock
    })

    if (data === LedgerErrorsCodes.BridgeNotReady ||
        data === LedgerErrorsCodes.CommandInProgress) {
      return this.createErrorFromCode(data)
    }

    if (!data.payload.success) {
      return data.payload
    }

    return { success: data.payload.success }
  }

  getAccounts = async (from: number, to: number): Promise<GetAccountsHardwareOperationResult> => {
    const result = await this.unlock()
    if (!result.success) {
      return result
    }
    from = (from >= 0) ? from : 0
    const paths = []
    const addZeroPath = (from > 0 || to < 0)
    if (addZeroPath) {
      // Add zero address to calculate device id.
      paths.push(this.getPathForIndex(0))
    }
    for (let i = from; i <= to; i++) {
      paths.push(this.getPathForIndex(i))
    }
    return this.getAccountsFromDevice(paths, addZeroPath)
  }

  signTransaction = async (path: string, rawTxBytes: Buffer): Promise<SignHardwareOperationResult> => {
    const result = await this.unlock()
    if (!result.success) {
      return result
    }

    const data = await this.sendCommand<SignTransactionResponsePayload>({
      command: LedgerCommand.SignTransaction,
      id: LedgerCommand.SignTransaction,
      path: path,
      rawTxBytes: rawTxBytes,
      origin: window.origin
    })
    if (data === LedgerErrorsCodes.BridgeNotReady ||
        data === LedgerErrorsCodes.CommandInProgress) {
      return this.createErrorFromCode(data)
    }
    const response: SignTransactionResponse = data.payload
    return { success: true, payload: response.signature }
  }

  private readonly createBridge = (targetUrl: string) => {
    return new Promise<HTMLIFrameElement>((resolve) => {
      let element = document.createElement('iframe')
      element.id = this.frameId
      element.src = targetUrl
      element.style.display = 'none'
        element.allow = 'hid'
      element.onload = () => {
        this.bridge = element
        resolve(element)
      }
      document.body.appendChild(element)
    })
  }

  private readonly getAccountsFromDevice = async (paths: string[], skipZeroPath: boolean): Promise<GetAccountsHardwareOperationResult> => {
    let accounts = []
    const zeroPath = this.getPathForIndex(0)
    for (const path of paths) {
      const data = await this.sendCommand<GetAccountResponsePayload>({
        command: LedgerCommand.GetAccount,
        id: LedgerCommand.GetAccount,
        path: path,
        origin: window.origin
      })
      if (data === LedgerErrorsCodes.BridgeNotReady ||
          data === LedgerErrorsCodes.CommandInProgress) {
        return this.createErrorFromCode(data)
      }

      if (!data.payload.success) {
        const ledgerError = data.payload as LedgerError
        return { success: false, error: ledgerError, code: ledgerError.statusCode } // TODO maybe code should be undefined
        // return { success: false, error: ledgerError.message, code: ledgerError.statusCode || ledgerError.id || ledgerError.name }
        // return data.payload // { success, error, code }
      }
      const response = data.payload as GetAccountResponse

      if (path === zeroPath) {
        this.deviceId = await hardwareDeviceIdFromAddress(response.address)
        if (skipZeroPath) {
          // If requested addresses do not have zero indexed adress we add it
          // intentionally to calculate device id and should not add it to
          // returned accounts
          continue
        }
      }

      accounts.push({
        address: '',
        addressBytes: response.address,
        derivationPath: path,
        name: this.type(),
        hardwareVendor: this.type(),
        deviceId: this.deviceId,
        coin: this.coin(),
        network: undefined
      })
    }
    return { success: true, payload: accounts }
  }

  sendCommand = async <T> (command: LedgerFrameCommand): Promise<T | LedgerErrorsCodes > => {
    if (!this.bridge && !this.hasBridgeCreated()) {
      this.bridge = await this.createBridge(kLedgerBridgeUrl)
    }
    if (!this.bridge || !this.bridge.contentWindow) {
      return LedgerErrorsCodes.BridgeNotReady
    }
    if (!this.transport) {
      this.transport = new LedgerTrustedMessagingTransport(this.bridge.contentWindow, kLedgerBridgeUrl, this.onAuthorized)
    }
    return this.transport.sendCommand(command)
  }

  cancelOperation = async () => {
    // TODO
    // this.transport?.close()
  }

  private readonly createErrorFromCode = (code: LedgerErrorsCodes): HardwareOperationResult => {
    switch (code) {
      case LedgerErrorsCodes.BridgeNotReady:
        return { success: false, error: getLocale('braveWalletBridgeNotReady'), code: code }
      case LedgerErrorsCodes.CommandInProgress:
        return { success: false, error: getLocale('braveWalletBridgeCommandInProgress'), code: code }
    }
  }
  private readonly getPathForIndex = (index: number): string => {
    return `44'/501'/${index}'/0'`
  }

  private readonly hasBridgeCreated = (): boolean => {
    return document.getElementById(this.frameId) !== null
  }
}
