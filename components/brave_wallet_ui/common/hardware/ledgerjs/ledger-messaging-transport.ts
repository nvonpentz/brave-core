/* Copyright (c) 2022 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

import {
  LedgerErrorsCodes,
  LedgerFrameCommand
} from './ledger-messages'

// LedgerMessagingTransport is a generic bi-directional messaging utility for 
// Window objects. It supports supports both (1) the sending of messages via postMessage
// to a window object at a targetUrl and subscribing of responses, and (2) 
// the definition of handlers to be run when a different LedgerMessagingTransport 
// instance at a different Window sends messages to it.
export abstract class LedgerMessagingTransport {
  protected targetWindow: Window
  protected targetUrl: string
  protected handlers: Map<string, Function>

  constructor(targetWindow: Window, targetUrl: string) {
    this.targetWindow = targetWindow
    this.targetUrl = (new URL(targetUrl)).origin // Use URL.origin to remove trailing '/' on targetUrl  
    this.handlers = new Map<string, Function>()
  }

  sendCommand = <T> (command: LedgerFrameCommand): Promise<T | LedgerErrorsCodes > => {
    return new Promise<T | LedgerErrorsCodes> (async (resolve) => {
      // Set handler for the response by passing the resolve function to be run
      // when targetWindow responds using the same command.id.
      // This allows us to simply `await` the sendCommand response
      if (!this.addCommandHandler(command.id, resolve)) {
        resolve(LedgerErrorsCodes.CommandInProgress)
        return
      }
      this.targetWindow.postMessage(command, this.targetUrl)
    })
  }

  protected addCommandHandler = (id: string, listener: Function): boolean => {
    if (!this.handlers.size) {
      this.addWindowMessageListener()
      this.handlers.clear()
    }
    if (this.handlers.has(id)) {
      return false
    }
    this.handlers.set(id, listener)
    return true
  }

  protected removeCommandHandler = (id: string) => {
    if (!this.handlers.has(id)) {
      return false
    }
    this.handlers.delete(id)
    if (!this.handlers.size) {
      this.removeWindowMessageListener()
    }
    return true
  }

  protected onMessageReceived = async (event: MessageEvent<LedgerFrameCommand>) => {
    if (event.type !== 'message' || event.origin !== this.targetUrl) {
      return
    }
  
    const message = event.data as LedgerFrameCommand
    if (!message || !this.handlers.has(message.command)) {
      return
    }
    const callback = this.handlers.get(message.command)
    if (!(typeof callback === 'function')) {
      return
    }
    const response = await callback(event.data)
    const isResponseMessage = (event.origin !== event.data.origin || event.type !== 'message' || !event.source)
    if (isResponseMessage) {
      this.removeCommandHandler(event.data.id)
      return
    }
    if (!response) {
      return
    }
    const target = event.source as Window
    target.postMessage(response, response.origin)
  }

  private readonly addWindowMessageListener = () => {
    window.addEventListener('message', this.onMessageReceived)
  }

  private readonly removeWindowMessageListener = () => {
    window.removeEventListener('message', this.onMessageReceived)
  }
}
