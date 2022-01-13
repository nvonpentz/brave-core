// Copyright (c) 2021 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// you can obtain one at http://mozilla.org/MPL/2.0/.

import { BraveWallet } from '../constants/types'
import {
  ALGOIconUrl,
  BATIconUrl,
  BNBIconUrl,
  BTCIconUrl,
  ETHIconUrl,
  ZRXIconUrl
} from '../assets/asset-icons'
import MoonCatIcon from '../assets/png-icons/mooncat.png'

export const ETH = {
  contractAddress: '0xeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee',
  name: 'Ethereum',
  symbol: 'ETH',
  logo: ETHIconUrl,
  isErc20: false,
  isErc721: false,
  decimals: 18,
  visible: true,
  tokenId: '',
  coingeckoId: ''
} as BraveWallet.BlockchainToken

export const BAT = {
  contractAddress: '0x0D8775F648430679A709E98d2b0Cb6250d2887EF',
  name: 'Basic Attention Token',
  symbol: 'BAT',
  logo: 'chrome://erc-token-images/bat.png',
  isErc20: true,
  isErc721: false,
  decimals: 18,
  visible: false,
  tokenId: '',
  coingeckoId: ''
} as BraveWallet.BlockchainToken

export const RopstenSwapAssetOptions: BraveWallet.BlockchainToken[] = [
  ETH,
  {
    contractAddress: '0xad6d458402f60fd3bd25163575031acdce07538d',
    name: 'DAI Stablecoin',
    symbol: 'DAI',
    logo: 'chrome://erc-token-images/dai.png',
    isErc20: true,
    isErc721: false,
    decimals: 18,
    visible: true,
    tokenId: '',
    coingeckoId: ''
  },
  {
    contractAddress: '0x07865c6e87b9f70255377e024ace6630c1eaa37f',
    name: 'USD Coin',
    symbol: 'USDC',
    logo: 'chrome://erc-token-images/usdc.png',
    isErc20: true,
    isErc721: false,
    decimals: 6,
    visible: true,
    tokenId: '',
    coingeckoId: ''
  }
]

// Use only with storybook as dummy data.
export const NewAssetOptions: BraveWallet.BlockchainToken[] = [
  {
    contractAddress: '1',
    name: 'Ethereum',
    symbol: 'ETH',
    logo: ETHIconUrl,
    isErc20: true,
    isErc721: false,
    decimals: 18,
    visible: true,
    tokenId: '',
    coingeckoId: ''
  },
  {
    contractAddress: '2',
    name: 'Basic Attention Token',
    symbol: 'BAT',
    logo: BATIconUrl,
    isErc20: true,
    isErc721: false,
    decimals: 18,
    visible: true,
    tokenId: '',
    coingeckoId: ''
  },
  {
    contractAddress: '3',
    name: 'Binance Coin',
    symbol: 'BNB',
    logo: BNBIconUrl,
    isErc20: true,
    isErc721: false,
    decimals: 18,
    visible: true,
    tokenId: '',
    coingeckoId: ''
  },
  {
    contractAddress: '4',
    name: 'Bitcoin',
    symbol: 'BTC',
    logo: BTCIconUrl,
    isErc20: true,
    isErc721: false,
    decimals: 18,
    visible: true,
    tokenId: '',
    coingeckoId: ''
  },
  {
    contractAddress: '5',
    name: 'Algorand',
    symbol: 'ALGO',
    logo: ALGOIconUrl,
    isErc20: true,
    isErc721: false,
    decimals: 18,
    visible: true,
    tokenId: '',
    coingeckoId: ''
  },
  {
    contractAddress: '6',
    name: '0x',
    symbol: 'ZRX',
    logo: ZRXIconUrl,
    isErc20: true,
    isErc721: false,
    decimals: 18,
    visible: true,
    tokenId: '',
    coingeckoId: ''
  },
  {
    contractAddress: '7',
    name: 'AcclimatedMoonCats',
    symbol: 'AMC',
    logo: MoonCatIcon,
    isErc20: false,
    isErc721: true,
    decimals: 0,
    visible: true,
    tokenId: '0x42a5',
    coingeckoId: ''
  }
]

// Use only with storybook as dummy data.
export const AccountAssetOptions: BraveWallet.BlockchainToken[] = [
  ETH,
  {
    contractAddress: '0x0D8775F648430679A709E98d2b0Cb6250d2887EF',
    name: 'Basic Attention Token',
    symbol: 'BAT',
    logo: BATIconUrl,
    isErc20: true,
    isErc721: false,
    decimals: 18,
    visible: true,
    tokenId: '',
    coingeckoId: ''
  },
  {
    contractAddress: '3',
    name: 'Binance Coin',
    symbol: 'BNB',
    logo: BNBIconUrl,
    isErc20: true,
    isErc721: false,
    decimals: 8,
    visible: true,
    tokenId: '',
    coingeckoId: ''
  },
  {
    contractAddress: '4',
    name: 'Bitcoin',
    symbol: 'BTC',
    logo: BTCIconUrl,
    isErc20: true,
    isErc721: false,
    decimals: 8,
    visible: true,
    tokenId: '',
    coingeckoId: ''
  },
  {
    contractAddress: '5',
    name: 'Algorand',
    symbol: 'ALGO',
    logo: ALGOIconUrl,
    isErc20: true,
    isErc721: false,
    decimals: 8,
    visible: true,
    tokenId: '',
    coingeckoId: ''
  },
  {
    contractAddress: '0xE41d2489571d322189246DaFA5ebDe1F4699F498',
    name: '0x',
    symbol: 'ZRX',
    logo: ZRXIconUrl,
    isErc20: true,
    isErc721: false,
    decimals: 18,
    visible: true,
    tokenId: '',
    coingeckoId: ''
  }
]
