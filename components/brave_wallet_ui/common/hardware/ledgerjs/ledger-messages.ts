/* Copyright (c) 2022 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import { loadTimeData } from '../../../../common/loadTimeData'
export const kLedgerBridgeUrl = loadTimeData.getString('braveWalletLedgerBridgeUrl')

export enum LedgerCommand {
  Unlock = 'ledger-unlock',
  GetAccount = 'ledger-get-accounts',
  SignTransaction = 'ledger-sign-transaction',
  AuthorizationRequired = 'authorization-required', // Sent by the frame to the parent context 
  AuthorizationSuccess = 'authorization-success' // Sent by the frame to the parent context 
}

export enum LedgerErrorsCodes {
  BridgeNotReady = 0,
  CommandInProgress = 1
}

export type CommandMessage = {
  command: LedgerCommand
  id: string
  origin: string
}

export type LedgerResponse = {
  success: boolean
}

export type LedgerError = LedgerResponse & {
  message?: string
  statusCode?: number
  id?: string
  name?: string
}

// Unlock command
export type UnlockResponsePayload = CommandMessage & {
  payload: LedgerResponse | LedgerError
}
export type UnlockCommand = CommandMessage & {
  command: LedgerCommand.Unlock
}

// GetAccounts command
export type GetAccountResponse = LedgerResponse & {
  address: Buffer
}
export type GetAccountResponsePayload = CommandMessage & {
  payload: GetAccountResponse | LedgerError
}
export type GetAccountCommand = CommandMessage & {
  command: LedgerCommand.GetAccount
  path: string
}

// SignTransaction command
export type SignTransactionResponse = {
  signature: Buffer
}
export type SignTransactionResponsePayload = CommandMessage & {
  payload: SignTransactionResponse
}
export type SignTransactionCommand = CommandMessage & {
  command: LedgerCommand.SignTransaction
  path: string,
  rawTxBytes: Buffer
}

// AuthorizationRequired command
export type AuthorizationRequiredCommand = CommandMessage & {
  command: LedgerCommand.AuthorizationRequired
}

// AuthorizationSuccess command
export type AuthorizationSuccessCommand = CommandMessage & {
  command: LedgerCommand.AuthorizationSuccess
}

export type LedgerFrameCommand = UnlockCommand | GetAccountCommand | SignTransactionCommand | AuthorizationRequiredCommand | AuthorizationSuccessCommand;
export type LedgerFrameResponse = UnlockResponsePayload | GetAccountResponsePayload | SignTransactionResponsePayload; 
