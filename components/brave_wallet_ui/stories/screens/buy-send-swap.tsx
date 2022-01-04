import * as React from 'react'
import {
  BuySendSwapTypes,
  UserAccountType,
  AccountAssetOptionType,
  OrderTypes,
  SlippagePresetObjectType,
  ExpirationPresetObjectType,
  ToOrFromType,
  BraveWallet,
  BuySupportedChains,
  SwapSupportedChains,
  SwapValidationErrorType,
  DefaultCurrencies
} from '../../constants/types'
import Swap from '../../components/buy-send-swap/tabs/swap-tab'
import Send from '../../components/buy-send-swap/tabs/send-tab'
import Buy from '../../components/buy-send-swap/tabs/buy-tab'
import {
  Layout
} from '../../components/buy-send-swap'

export interface Props {
  accounts: UserAccountType[]
  networkList: BraveWallet.EthereumChain[]
  orderType: OrderTypes
  selectedSendAsset: AccountAssetOptionType
  sendAssetBalance: string
  swapToAsset: AccountAssetOptionType
  swapFromAsset: AccountAssetOptionType
  selectedNetwork: BraveWallet.EthereumChain
  selectedAccount: UserAccountType
  selectedTab: BuySendSwapTypes
  exchangeRate: string
  slippageTolerance: SlippagePresetObjectType
  swapValidationError?: SwapValidationErrorType
  orderExpiration: ExpirationPresetObjectType
  buyAmount: string
  sendAmount: string
  fromAmount: string
  toAmount: string
  fromAssetBalance: string
  toAssetBalance: string
  toAddressOrUrl: string
  toAddress: string
  addressError: string
  addressWarning: string
  buyAssetOptions: AccountAssetOptionType[]
  sendAssetOptions: AccountAssetOptionType[]
  swapAssetOptions: AccountAssetOptionType[]
  isFetchingSwapQuote: boolean
  isSwapSubmitDisabled: boolean
  customSlippageTolerance: string
  defaultCurrencies: DefaultCurrencies
  onCustomSlippageToleranceChange: (value: string) => void
  onSubmitBuy: (asset: AccountAssetOptionType) => void
  onSubmitSend: () => void
  onSubmitSwap: () => void
  flipSwapAssets: () => void
  onSelectNetwork: (network: BraveWallet.EthereumChain) => void
  onSelectAccount: (account: UserAccountType) => void
  onToggleOrderType: () => void
  onSelectAsset: (asset: AccountAssetOptionType, toOrFrom: ToOrFromType) => void
  onSelectSlippageTolerance: (slippage: SlippagePresetObjectType) => void
  onSelectExpiration: (expiration: ExpirationPresetObjectType) => void
  onSetExchangeRate: (value: string) => void
  onSetBuyAmount: (value: string) => void
  onSetSendAmount: (value: string) => void
  onSetFromAmount: (value: string) => void
  onSetToAddressOrUrl: (value: string) => void
  onSetToAmount: (value: string) => void
  onSelectPresetFromAmount: (percent: number) => void
  onSelectPresetSendAmount: (percent: number) => void
  onSelectTab: (tab: BuySendSwapTypes) => void
  onSwapQuoteRefresh: () => void
  onSelectSendAsset: (asset: AccountAssetOptionType, toOrFrom: ToOrFromType) => void
  onAddNetwork: () => void
  onAddAsset: (value: boolean) => void
}

function BuySendSwap (props: Props) {
  const {
    accounts,
    networkList,
    orderType,
    swapToAsset,
    swapFromAsset,
    selectedNetwork,
    selectedAccount,
    selectedTab,
    exchangeRate,
    slippageTolerance,
    orderExpiration,
    buyAmount,
    sendAmount,
    fromAmount,
    toAmount,
    addressError,
    addressWarning,
    selectedSendAsset,
    sendAssetBalance,
    fromAssetBalance,
    toAssetBalance,
    toAddress,
    toAddressOrUrl,
    buyAssetOptions,
    sendAssetOptions,
    swapAssetOptions,
    swapValidationError,
    isFetchingSwapQuote,
    isSwapSubmitDisabled,
    customSlippageTolerance,
    defaultCurrencies,
    onCustomSlippageToleranceChange,
    onSubmitBuy,
    onSubmitSend,
    onSubmitSwap,
    flipSwapAssets,
    onSelectNetwork,
    onSelectAccount,
    onToggleOrderType,
    onSelectAsset,
    onSelectSlippageTolerance,
    onSelectExpiration,
    onSetExchangeRate,
    onSetBuyAmount,
    onSetSendAmount,
    onSetFromAmount,
    onSetToAddressOrUrl,
    onSetToAmount,
    onSelectPresetFromAmount,
    onSelectPresetSendAmount,
    onSelectTab,
    onSwapQuoteRefresh,
    onSelectSendAsset,
    onAddNetwork,
    onAddAsset
  } = props

  // Switched this to useLayoutEffect to fix bad setState call error
  // that was accouring when you would switch to a network that doesn't
  // support swap and buy.
  React.useLayoutEffect(() => {
    if (selectedTab === 'buy' && !BuySupportedChains.includes(selectedNetwork.chainId)) {
      onSelectTab('send')
    }
    if (selectedTab === 'swap' && !SwapSupportedChains.includes(selectedNetwork.chainId)) {
      onSelectTab('send')
    }
  }, [selectedNetwork, selectedTab, BuySupportedChains])

  const isBuyDisabled = React.useMemo(() => {
    return !BuySupportedChains.includes(selectedNetwork.chainId)
  }, [BuySupportedChains, selectedNetwork])

  const isSwapDisabled = React.useMemo(() => {
    return !SwapSupportedChains.includes(selectedNetwork.chainId)
  }, [SwapSupportedChains, selectedNetwork])

  const changeTab = (tab: BuySendSwapTypes) => () => {
    onSelectTab(tab)
  }

  const onClickAddAsset = () => {
    onAddAsset(true)
  }

  return (
    <Layout
      selectedNetwork={selectedNetwork}
      isBuyDisabled={isBuyDisabled}
      isSwapDisabled={isSwapDisabled}
      selectedTab={selectedTab}
      onChangeTab={changeTab}
    >
      {selectedTab === 'swap' &&
        <Swap
          accounts={accounts}
          networkList={networkList}
          orderType={orderType}
          swapToAsset={swapToAsset}
          swapFromAsset={swapFromAsset}
          selectedNetwork={selectedNetwork}
          selectedAccount={selectedAccount}
          exchangeRate={exchangeRate}
          orderExpiration={orderExpiration}
          slippageTolerance={slippageTolerance}
          fromAmount={fromAmount}
          toAmount={toAmount}
          fromAssetBalance={fromAssetBalance}
          toAssetBalance={toAssetBalance}
          isFetchingQuote={isFetchingSwapQuote}
          isSubmitDisabled={isSwapSubmitDisabled}
          validationError={swapValidationError}
          customSlippageTolerance={customSlippageTolerance}
          onCustomSlippageToleranceChange={onCustomSlippageToleranceChange}
          onSubmitSwap={onSubmitSwap}
          flipSwapAssets={flipSwapAssets}
          onSelectNetwork={onSelectNetwork}
          onSelectAccount={onSelectAccount}
          onSelectSwapAsset={onSelectAsset}
          onToggleOrderType={onToggleOrderType}
          onSelectSlippageTolerance={onSelectSlippageTolerance}
          onSelectExpiration={onSelectExpiration}
          onSetExchangeRate={onSetExchangeRate}
          onSetFromAmount={onSetFromAmount}
          onSetToAmount={onSetToAmount}
          onSelectPresetAmount={onSelectPresetFromAmount}
          assetOptions={swapAssetOptions}
          onQuoteRefresh={onSwapQuoteRefresh}
          onAddNetwork={onAddNetwork}
          onAddAsset={onClickAddAsset}
        />
      }
      {selectedTab === 'send' &&
        <Send
          addressError={addressError}
          addressWarning={addressWarning}
          accounts={accounts}
          networkList={networkList}
          selectedAssetAmount={sendAmount}
          selectedAssetBalance={sendAssetBalance}
          toAddressOrUrl={toAddressOrUrl}
          toAddress={toAddress}
          onSelectAccount={onSelectAccount}
          onSelectNetwork={onSelectNetwork}
          onSelectPresetAmount={onSelectPresetSendAmount}
          onSelectAsset={onSelectSendAsset}
          onSetSendAmount={onSetSendAmount}
          onSetToAddressOrUrl={onSetToAddressOrUrl}
          onSubmit={onSubmitSend}
          selectedAccount={selectedAccount}
          selectedNetwork={selectedNetwork}
          selectedAsset={selectedSendAsset}
          showHeader={true}
          assetOptions={sendAssetOptions}
          onAddNetwork={onAddNetwork}
          onAddAsset={onClickAddAsset}
        />
      }
      {selectedTab === 'buy' &&
        <Buy
          defaultCurrencies={defaultCurrencies}
          accounts={accounts}
          networkList={networkList}
          buyAmount={buyAmount}
          onSelectAccount={onSelectAccount}
          onSelectNetwork={onSelectNetwork}
          onSubmit={onSubmitBuy}
          onSetBuyAmount={onSetBuyAmount}
          selectedAccount={selectedAccount}
          selectedNetwork={selectedNetwork}
          showHeader={true}
          assetOptions={buyAssetOptions}
          onAddNetwork={onAddNetwork}
          onAddAsset={onClickAddAsset}
        />
      }
    </Layout>
  )
}

export default BuySendSwap
