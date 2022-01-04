import { AccountAssetOptionType, BraveWallet } from '../constants/types'
import {
  ALGOIconUrl,
  BATIconUrl,
  BNBIconUrl,
  BTCIconUrl,
  ETHIconUrl,
  ZRXIconUrl
} from '../assets/asset-icons'

export const ETH: AccountAssetOptionType = {
  asset: {
    contractAddress: '0xeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeeee',
    name: 'Ethereum',
    symbol: 'ETH',
    logo: ETHIconUrl,
    isErc20: false,
    isErc721: false,
    decimals: 18,
    visible: true,
    tokenId: ''
  },
  assetBalance: '0',
  fiatBalance: '0'
}

export const BAT: AccountAssetOptionType = {
  asset: {
    contractAddress: '0x0D8775F648430679A709E98d2b0Cb6250d2887EF',
    name: 'Basic Attention Token',
    symbol: 'BAT',
    logo: 'chrome://erc-token-images/bat.png',
    isErc20: true,
    isErc721: false,
    decimals: 18,
    visible: false,
    tokenId: ''
  },
  assetBalance: '0',
  fiatBalance: '0'
}

export const RopstenSwapAssetOptions: AccountAssetOptionType[] = [
  ETH,
  {
    asset: {
      contractAddress: '0xad6d458402f60fd3bd25163575031acdce07538d',
      name: 'DAI Stablecoin',
      symbol: 'DAI',
      logo: 'chrome://erc-token-images/dai.png',
      isErc20: true,
      isErc721: false,
      decimals: 18,
      visible: true,
      tokenId: ''
    },
    assetBalance: '0',
    fiatBalance: '0'
  },
  {
    asset: {
      contractAddress: '0x07865c6e87b9f70255377e024ace6630c1eaa37f',
      name: 'USD Coin',
      symbol: 'USDC',
      logo: 'chrome://erc-token-images/usdc.png',
      isErc20: true,
      isErc721: false,
      decimals: 6,
      visible: true,
      tokenId: ''
    },
    assetBalance: '0',
    fiatBalance: '0'
  }
]

// Use only with storybook as dummy data.
export const NewAssetOptions: BraveWallet.ERCToken[] = [
  {
    contractAddress: '1',
    name: 'Ethereum',
    symbol: 'ETH',
    logo: ETHIconUrl,
    isErc20: true,
    isErc721: false,
    decimals: 18,
    visible: true,
    tokenId: ''
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
    tokenId: ''
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
    tokenId: ''
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
    tokenId: ''
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
    tokenId: ''
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
    tokenId: ''
  }
]

// Use only with storybook as dummy data.
export const AccountAssetOptions: AccountAssetOptionType[] = [
  ETH,
  {
    asset: {
      contractAddress: '0x0D8775F648430679A709E98d2b0Cb6250d2887EF',
      name: 'Basic Attention Token',
      symbol: 'BAT',
      logo: BATIconUrl,
      isErc20: true,
      isErc721: false,
      decimals: 18,
      visible: true,
      tokenId: ''
    },
    assetBalance: '0',
    fiatBalance: '0'
  },
  {
    asset: {
      contractAddress: '3',
      name: 'Binance Coin',
      symbol: 'BNB',
      logo: BNBIconUrl,
      isErc20: true,
      isErc721: false,
      decimals: 8,
      visible: true,
      tokenId: ''
    },
    assetBalance: '0',
    fiatBalance: '0'
  },
  {
    asset: {
      contractAddress: '4',
      name: 'Bitcoin',
      symbol: 'BTC',
      logo: BTCIconUrl,
      isErc20: true,
      isErc721: false,
      decimals: 8,
      visible: true,
      tokenId: ''
    },
    assetBalance: '0',
    fiatBalance: '0'
  },
  {
    asset: {
      contractAddress: '5',
      name: 'Algorand',
      symbol: 'ALGO',
      logo: ALGOIconUrl,
      isErc20: true,
      isErc721: false,
      decimals: 8,
      visible: true,
      tokenId: ''
    },
    assetBalance: '0',
    fiatBalance: '0'
  },
  {
    asset: {
      contractAddress: '0xE41d2489571d322189246DaFA5ebDe1F4699F498',
      name: '0x',
      symbol: 'ZRX',
      logo: ZRXIconUrl,
      isErc20: true,
      isErc721: false,
      decimals: 18,
      visible: true,
      tokenId: ''
    },
    assetBalance: '0',
    fiatBalance: '0'
  }
]
