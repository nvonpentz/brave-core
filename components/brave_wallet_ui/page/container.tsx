// Copyright (c) 2020 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// you can obtain one at http://mozilla.org/MPL/2.0/.

import * as React from 'react'
import { connect } from 'react-redux'
import { bindActionCreators, Dispatch } from 'redux'
import { Switch, Route, useHistory, useLocation } from 'react-router-dom'

import * as WalletPageActions from './actions/wallet_page_actions'
import * as WalletActions from '../common/actions/wallet_actions'
import store from './store'

import 'emptykit.css'
import '../../../ui/webui/resources/fonts/poppins.css'
import '../../../ui/webui/resources/fonts/muli.css'

import { WalletWidgetStandIn, OnboardingWrapper } from '../stories/style'
import {
  // SideNav,
  WalletPageLayout,
  WalletSubViewLayout,
  CryptoView,
  LockScreen,
  OnboardingRestore
} from '../components/desktop'
import {
  BraveWallet,
  WalletState,
  PageState,
  WalletPageState,
  AccountAssetOptionType,
  WalletAccountType,
  UpdateAccountNamePayloadType,
  WalletRoutes,
  BuySendSwapTypes
} from '../constants/types'
// import { NavOptions } from '../options/side-nav-options'
import BuySendSwap from '../stories/screens/buy-send-swap'
import Onboarding from '../stories/screens/onboarding'
import BackupWallet from '../stories/screens/backup-wallet'

// Utils
import { formatWithCommasAndDecimals } from '../utils/format-prices'
import { GetBuyOrFaucetUrl } from '../utils/buy-asset-url'
import { mojoTimeDeltaToJSDate } from '../utils/datetime-utils'
import { stripERC20TokenImageURL } from '../utils/string-utils'
import { addNumericValues } from '../utils/bn-utils'

import {
  findENSAddress,
  findUnstoppableDomainAddress,
  getBalance,
  getChecksumEthAddress,
  getERC20Allowance,
  onConnectHardwareWallet,
  isStrongPassword,
  getBlockchainTokenInfo,
  getBuyAssets
} from '../common/async/lib'

// Hooks
import {
  useSwap,
  useAssets,
  useBalance,
  useSend,
  usePreset,
  useTokenInfo,
  usePricing
} from '../common/hooks'

type Props = {
  wallet: WalletState
  page: PageState
  walletPageActions: typeof WalletPageActions
  walletActions: typeof WalletActions
}

function Container (props: Props) {
  let history = useHistory()
  const { pathname: walletLocation } = useLocation()
  // Wallet Props
  const {
    isWalletCreated,
    isWalletLocked,
    isWalletBackedUp,
    hasIncorrectPassword,
    accounts,
    networkList,
    transactions,
    selectedNetwork,
    selectedAccount,
    hasInitialized,
    portfolioPriceHistory,
    selectedPortfolioTimeline,
    isFetchingPortfolioPriceHistory,
    transactionSpotPrices,
    addUserAssetError,
    defaultWallet,
    isMetaMaskInstalled,
    defaultCurrencies
  } = props.wallet

  // Page Props
  const {
    invalidMnemonic,
    mnemonic,
    selectedTimeline,
    selectedAsset,
    selectedAssetFiatPrice,
    selectedAssetCryptoPrice,
    selectedAssetPriceHistory,
    setupStillInProgress,
    isFetchingPriceHistory,
    privateKey,
    importAccountError,
    importWalletError,
    showAddModal,
    isCryptoWalletsInstalled,
    swapQuote,
    swapError
  } = props.page

  // const [view, setView] = React.useState<NavTypes>('crypto')
  const [inputValue, setInputValue] = React.useState<string>('')
  const [buyAmount, setBuyAmount] = React.useState('')
  const [selectedWidgetTab, setSelectedWidgetTab] = React.useState<BuySendSwapTypes>('buy')
  const [showVisibleAssetsModal, setShowVisibleAssetsModal] = React.useState<boolean>(false)

  const {
    tokenOptions,
    assetOptions,
    userVisibleTokenOptions,
    sendAssetOptions,
    buyAssetOptions
  } = useAssets(
    accounts,
    selectedAccount,
    props.wallet.fullTokenList,
    props.wallet.userVisibleTokensInfo,
    transactionSpotPrices,
    getBuyAssets
  )

  const {
    onFindTokenInfoByContractAddress,
    foundTokenInfoByContractAddress
  } = useTokenInfo(getBlockchainTokenInfo, userVisibleTokenOptions, tokenOptions, selectedNetwork)

  const {
    exchangeRate,
    fromAmount,
    fromAsset,
    isFetchingSwapQuote,
    isSwapButtonDisabled,
    orderExpiration,
    orderType,
    slippageTolerance,
    swapValidationError,
    toAmount,
    toAsset,
    swapAssetOptions,
    customSlippageTolerance,
    onToggleOrderType,
    onSwapQuoteRefresh,
    onSetToAmount,
    onSetFromAmount,
    flipSwapAssets,
    onSubmitSwap,
    onSetExchangeRate,
    onSelectExpiration,
    onSelectSlippageTolerance,
    onSelectTransactAsset,
    onCustomSlippageToleranceChange
  } = useSwap(
    selectedAccount,
    selectedNetwork,
    assetOptions,
    props.walletPageActions.fetchPageSwapQuote,
    getERC20Allowance,
    props.walletActions.approveERC20Allowance,
    swapQuote,
    swapError
  )

  const {
    onSetSendAmount,
    onSetToAddressOrUrl,
    onSubmitSend,
    onSelectSendAsset,
    sendAmount,
    toAddressOrUrl,
    toAddress,
    addressError,
    addressWarning,
    selectedSendAsset
  } = useSend(
    findENSAddress,
    findUnstoppableDomainAddress,
    getChecksumEthAddress,
    sendAssetOptions,
    selectedAccount,
    props.walletActions.sendERC20Transfer,
    props.walletActions.sendTransaction,
    props.walletActions.sendERC721TransferFrom,
    props.wallet.fullTokenList
  )

  const { computeFiatAmount } = usePricing(transactionSpotPrices)
  const getAccountBalance = useBalance(selectedNetwork)
  const sendAssetBalance = getAccountBalance(selectedAccount, selectedSendAsset?.asset)
  const fromAssetBalance = getAccountBalance(selectedAccount, fromAsset?.asset)
  const toAssetBalance = getAccountBalance(selectedAccount, toAsset?.asset)

  const onSelectPresetAmountFactory = usePreset(selectedAccount, fromAsset, selectedSendAsset, onSetFromAmount, onSetSendAmount)

  const onToggleShowRestore = React.useCallback(() => {
    if (walletLocation === WalletRoutes.Restore) {
      history.goBack()
    } else {
      history.push(WalletRoutes.Restore)
    }
  }, [walletLocation])

  const onSetBuyAmount = (value: string) => {
    setBuyAmount(value)
  }

  const onSelectAccount = (account: WalletAccountType) => {
    props.walletActions.selectAccount(account)
  }

  const onSelectNetwork = (network: BraveWallet.EthereumChain) => {
    props.walletActions.selectNetwork(network)
  }

  const completeWalletSetup = (recoveryVerified: boolean) => {
    if (recoveryVerified) {
      props.walletPageActions.walletBackupComplete()
    }
    props.walletPageActions.walletSetupComplete()
  }

  const onBackupWallet = () => {
    props.walletPageActions.walletBackupComplete()
    history.goBack()
  }

  const restoreWallet = (mnemonic: string, password: string, isLegacy: boolean) => {
    props.walletPageActions.restoreWallet({ mnemonic, password, isLegacy })
  }

  const passwordProvided = (password: string) => {
    props.walletPageActions.createWallet({ password })
  }

  const unlockWallet = () => {
    props.walletActions.unlockWallet({ password: inputValue })
    setInputValue('')
  }

  const lockWallet = () => {
    props.walletActions.lockWallet()
  }

  const onShowBackup = () => {
    props.walletPageActions.showRecoveryPhrase(true)
    history.push(WalletRoutes.Backup)
  }

  const onHideBackup = () => {
    props.walletPageActions.showRecoveryPhrase(false)
    history.goBack()
  }

  const handlePasswordChanged = (value: string) => {
    setInputValue(value)
    if (hasIncorrectPassword) {
      props.walletActions.hasIncorrectPassword(false)
    }
  }

  const restoreError = React.useMemo(() => {
    if (invalidMnemonic) {
      setTimeout(function () { props.walletPageActions.hasMnemonicError(false) }, 5000)
      return true
    }
    return false
  }, [invalidMnemonic])

  const recoveryPhrase = React.useMemo(() => {
    return (mnemonic || '').split(' ')
  }, [mnemonic])

  // This will scrape all the user's accounts and combine the asset balances for a single asset
  const fullAssetBalance = React.useCallback((asset: BraveWallet.BlockchainToken) => {
    const amounts = accounts.map((account) =>
      getAccountBalance(account, asset))

    return amounts.reduce(function (a, b) {
      return a !== '' && b !== ''
        ? addNumericValues(a, b)
        : ''
    })
  }, [accounts, getAccountBalance])

  // This looks at the users asset list and returns the full balance for each asset
  const userAssetList = React.useMemo(() => {
    return userVisibleTokenOptions.map((asset) => ({
      asset: asset,
      assetBalance: fullAssetBalance(asset)
    }) as AccountAssetOptionType)
  }, [userVisibleTokenOptions, fullAssetBalance])

  const onSelectAsset = (asset: BraveWallet.BlockchainToken) => {
    props.walletPageActions.selectAsset({ asset: asset, timeFrame: selectedTimeline })
  }

  // This will scrape all of the user's accounts and combine the fiat value for every asset
  const fullPortfolioBalance = React.useMemo(() => {
    const visibleAssetOptions = userAssetList
      .filter((token) => token.asset.visible && !token.asset.isErc721)

    if (visibleAssetOptions.length === 0) {
      return ''
    }

    const visibleAssetFiatBalances = visibleAssetOptions
      .map((item) => {
        return computeFiatAmount(item.assetBalance, item.asset.symbol, item.asset.decimals)
      })

    const grandTotal = visibleAssetFiatBalances.reduce(function (a, b) {
      return a !== '' && b !== ''
        ? addNumericValues(a, b)
        : ''
    })
    return formatWithCommasAndDecimals(grandTotal)
  }, [userAssetList, fullAssetBalance, computeFiatAmount])

  const onChangeTimeline = (timeline: BraveWallet.AssetPriceTimeframe) => {
    if (selectedAsset) {
      props.walletPageActions.selectAsset({ asset: selectedAsset, timeFrame: timeline })
    } else {
      props.walletActions.selectPortfolioTimeline(timeline)
    }
  }

  const formattedPriceHistory = React.useMemo(() => {
    const formatted = selectedAssetPriceHistory.map((obj) => {
      return {
        date: mojoTimeDeltaToJSDate(obj.date),
        close: Number(obj.price)
      }
    })
    return formatted
  }, [selectedAssetPriceHistory])

  const onShowAddModal = () => {
    props.walletPageActions.setShowAddModal(true)
  }

  const onHideAddModal = () => {
    props.walletPageActions.setShowAddModal(false)
  }

  const onCreateAccount = (name: string) => {
    const created = props.walletPageActions.addAccount({ accountName: name })
    if (created) {
      onHideAddModal()
    }
  }

  const onSubmitBuy = (asset: AccountAssetOptionType) => {
    GetBuyOrFaucetUrl(selectedNetwork.chainId, asset, selectedAccount, buyAmount)
      .then(url => window.open(url, '_blank'))
      .catch(e => console.error(e))
  }

  const onAddHardwareAccounts = (selected: BraveWallet.HardwareWalletAccount[]) => {
    props.walletPageActions.addHardwareAccounts(selected)
  }

  const onImportAccount = (accountName: string, privateKey: string) => {
    props.walletPageActions.importAccount({ accountName, privateKey })
  }

  const onImportFilecoinAccount = (accountName: string, privateKey: string, network: string, protocol: BraveWallet.FilecoinAddressProtocol) => {
    props.walletPageActions.importFilecoinAccount({ accountName, privateKey, network, protocol })
  }

  const onImportAccountFromJson = (accountName: string, password: string, json: string) => {
    props.walletPageActions.importAccountFromJson({ accountName, password, json })
  }

  const onSetImportAccountError = (hasError: boolean) => {
    props.walletPageActions.setImportAccountError(hasError)
  }

  const onSetImportWalletError = (hasError: boolean) => {
    props.walletPageActions.setImportWalletError({ hasError })
  }

  const onRemoveAccount = (address: string, hardware: boolean) => {
    if (hardware) {
      props.walletPageActions.removeHardwareAccount({ address })
      return
    }
    props.walletPageActions.removeImportedAccount({ address })
  }

  const onUpdateAccountName = (payload: UpdateAccountNamePayloadType): { success: boolean } => {
    const result = props.walletPageActions.updateAccountName(payload)
    return result ? { success: true } : { success: false }
  }

  const fetchFullTokenList = () => {
    props.walletActions.getAllTokensList()
  }

  const onViewPrivateKey = (address: string, isDefault: boolean) => {
    props.walletPageActions.viewPrivateKey({ address, isDefault })
  }

  const onDoneViewingPrivateKey = () => {
    props.walletPageActions.doneViewingPrivateKey()
  }

  const onImportCryptoWallets = (password: string, newPassword: string) => {
    props.walletPageActions.importFromCryptoWallets({ password, newPassword })
  }

  const onImportMetaMask = (password: string, newPassword: string) => {
    props.walletPageActions.importFromMetaMask({ password, newPassword })
  }

  const checkWalletsToImport = () => {
    props.walletPageActions.checkWalletsToImport()
  }

  const onSetUserAssetVisible = (token: BraveWallet.BlockchainToken, isVisible: boolean) => {
    props.walletActions.setUserAssetVisible({ token, chainId: selectedNetwork.chainId, isVisible })
  }

  const onAddUserAsset = (token: BraveWallet.BlockchainToken) => {
    props.walletActions.addUserAsset({
      token: {
        ...token,
        logo: stripERC20TokenImageURL(token.logo) || ''
      },
      chainId: selectedNetwork.chainId
    })
  }

  const onRemoveUserAsset = (token: BraveWallet.BlockchainToken) => {
    props.walletActions.removeUserAsset({ token, chainId: selectedNetwork.chainId })
  }

  const onOpenWalletSettings = () => {
    props.walletPageActions.openWalletSettings()
  }

  const onRetryTransaction = (transaction: BraveWallet.TransactionInfo) => {
    props.walletActions.retryTransaction(transaction)
  }

  const onSpeedupTransaction = (transaction: BraveWallet.TransactionInfo) => {
    props.walletActions.speedupTransaction(transaction)
  }

  const onCancelTransaction = (transaction: BraveWallet.TransactionInfo) => {
    props.walletActions.cancelTransaction(transaction)
  }

  const onAddNetwork = () => {
    props.walletActions.expandWalletNetworks()
  }

  const onShowVisibleAssetsModal = (showModal: boolean) => {
    if (showModal) {
      if (tokenOptions.length === 0) {
        fetchFullTokenList()
      }
      history.push(`${WalletRoutes.AddAssetModal}`)
    } else {
      history.push(`${WalletRoutes.Portfolio}`)
    }
    setShowVisibleAssetsModal(showModal)
  }

  React.useEffect(() => {
    // Creates a list of Accepted Portfolio Routes
    const acceptedPortfolioRoutes = userVisibleTokenOptions.map((token) => {
      return `${WalletRoutes.Portfolio}/${token.symbol}`
    })
    // Creates a list of Accepted Account Routes
    const acceptedAccountRoutes = accounts.map((account) => {
      return `${WalletRoutes.Accounts}/${account.address}`
    })
    if (!hasInitialized) {
      return
    }
    // If wallet is not yet created will Route to Onboarding
    if ((!isWalletCreated || setupStillInProgress) && walletLocation !== WalletRoutes.Restore) {
      checkWalletsToImport()
      history.push(WalletRoutes.Onboarding)
      // If wallet is created will Route to login
    } else if (isWalletLocked && walletLocation !== WalletRoutes.Restore) {
      history.push(WalletRoutes.Unlock)
      // Allowed Private Routes if a wallet is unlocked else will redirect back to Portfolio
    } else if (
      !isWalletLocked &&
      hasInitialized &&
      walletLocation !== WalletRoutes.Backup &&
      walletLocation !== WalletRoutes.Accounts &&
      walletLocation !== WalletRoutes.AddAccountModal &&
      walletLocation !== WalletRoutes.AddAssetModal &&
      acceptedAccountRoutes.length !== 0 &&
      !acceptedAccountRoutes.includes(walletLocation) &&
      walletLocation !== WalletRoutes.Portfolio &&
      acceptedPortfolioRoutes.length !== 0 &&
      !acceptedPortfolioRoutes.includes(walletLocation)
    ) {
      history.push(WalletRoutes.Portfolio)
    }
  }, [
    walletLocation,
    isWalletCreated,
    isWalletLocked,
    hasInitialized,
    setupStillInProgress,
    selectedAsset,
    userVisibleTokenOptions,
    accounts
  ])

  const hideMainComponents = (isWalletCreated && !setupStillInProgress) && !isWalletLocked && walletLocation !== WalletRoutes.Backup

  return (
    <WalletPageLayout>
      {/* <SideNav
        navList={NavOptions}
        selectedButton={view}
        onSubmit={navigateTo}
      /> */}
      <Switch>
        <WalletSubViewLayout>
          <Route path={WalletRoutes.Restore} exact={true}>
            {isWalletLocked &&
              <OnboardingWrapper>
                <OnboardingRestore
                  checkIsStrongPassword={isStrongPassword}
                  onRestore={restoreWallet}
                  toggleShowRestore={onToggleShowRestore}
                  hasRestoreError={restoreError}
                />
              </OnboardingWrapper>
            }
          </Route>
          <Route path={WalletRoutes.Onboarding} exact={true}>
            <Onboarding
              checkIsStrongPassword={isStrongPassword}
              recoveryPhrase={recoveryPhrase}
              onPasswordProvided={passwordProvided}
              onSubmit={completeWalletSetup}
              onShowRestore={onToggleShowRestore}
              braveLegacyWalletDetected={isCryptoWalletsInstalled}
              metaMaskWalletDetected={isMetaMaskInstalled}
              importError={importWalletError}
              onSetImportError={onSetImportWalletError}
              onImportCryptoWallets={onImportCryptoWallets}
              onImportMetaMask={onImportMetaMask}
            />
          </Route>
          <Route path={WalletRoutes.Unlock} exact={true}>
            {isWalletLocked &&
              <OnboardingWrapper>
                <LockScreen
                  onSubmit={unlockWallet}
                  disabled={inputValue === ''}
                  onPasswordChanged={handlePasswordChanged}
                  hasPasswordError={hasIncorrectPassword}
                  onShowRestore={onToggleShowRestore}
                />
              </OnboardingWrapper>
            }
          </Route>
          <Route path={WalletRoutes.Backup} exact={true}>
            <OnboardingWrapper>
              <BackupWallet
                isOnboarding={false}
                onCancel={onHideBackup}
                onSubmit={onBackupWallet}
                recoveryPhrase={recoveryPhrase}
              />
            </OnboardingWrapper>
          </Route>
          <Route path={WalletRoutes.CryptoPage} exact={true}>
            {hideMainComponents &&
              <CryptoView
                defaultCurrencies={defaultCurrencies}
                onLockWallet={lockWallet}
                needsBackup={!isWalletBackedUp}
                onShowBackup={onShowBackup}
                accounts={accounts}
                networkList={networkList}
                onChangeTimeline={onChangeTimeline}
                onSelectAsset={onSelectAsset}
                portfolioBalance={fullPortfolioBalance}
                selectedAsset={selectedAsset}
                selectedAssetFiatPrice={selectedAssetFiatPrice}
                selectedAssetCryptoPrice={selectedAssetCryptoPrice}
                selectedAssetPriceHistory={formattedPriceHistory}
                portfolioPriceHistory={portfolioPriceHistory}
                selectedTimeline={selectedTimeline}
                selectedPortfolioTimeline={selectedPortfolioTimeline}
                transactions={transactions}
                userAssetList={userAssetList}
                fullAssetList={tokenOptions}
                onConnectHardwareWallet={onConnectHardwareWallet}
                onCreateAccount={onCreateAccount}
                onImportAccount={onImportAccount}
                onImportFilecoinAccount={onImportFilecoinAccount}
                isLoading={isFetchingPriceHistory}
                showAddModal={showAddModal}
                onHideAddModal={onHideAddModal}
                onUpdateAccountName={onUpdateAccountName}
                selectedNetwork={selectedNetwork}
                onSelectNetwork={onSelectNetwork}
                isFetchingPortfolioPriceHistory={isFetchingPortfolioPriceHistory}
                onRemoveAccount={onRemoveAccount}
                privateKey={privateKey ?? ''}
                onDoneViewingPrivateKey={onDoneViewingPrivateKey}
                onViewPrivateKey={onViewPrivateKey}
                onImportAccountFromJson={onImportAccountFromJson}
                onSetImportError={onSetImportAccountError}
                hasImportError={importAccountError}
                onAddHardwareAccounts={onAddHardwareAccounts}
                transactionSpotPrices={transactionSpotPrices}
                userVisibleTokensInfo={userVisibleTokenOptions}
                getBalance={getBalance}
                onAddUserAsset={onAddUserAsset}
                onSetUserAssetVisible={onSetUserAssetVisible}
                onRemoveUserAsset={onRemoveUserAsset}
                addUserAssetError={addUserAssetError}
                defaultWallet={defaultWallet}
                onOpenWalletSettings={onOpenWalletSettings}
                onShowAddModal={onShowAddModal}
                isMetaMaskInstalled={isMetaMaskInstalled}
                onRetryTransaction={onRetryTransaction}
                onSpeedupTransaction={onSpeedupTransaction}
                onCancelTransaction={onCancelTransaction}
                onShowVisibleAssetsModal={onShowVisibleAssetsModal}
                showVisibleAssetsModal={showVisibleAssetsModal}
                onFindTokenInfoByContractAddress={onFindTokenInfoByContractAddress}
                foundTokenInfoByContractAddress={foundTokenInfoByContractAddress}
              />
            }
          </Route>
        </WalletSubViewLayout>
      </Switch>
      {hideMainComponents &&
        <WalletWidgetStandIn>
          <BuySendSwap
            accounts={accounts}
            networkList={networkList}
            orderType={orderType}
            swapToAsset={toAsset}
            swapFromAsset={fromAsset}
            selectedNetwork={selectedNetwork}
            selectedAccount={selectedAccount}
            selectedTab={selectedWidgetTab}
            exchangeRate={exchangeRate}
            buyAmount={buyAmount}
            sendAmount={sendAmount}
            fromAmount={fromAmount}
            fromAssetBalance={fromAssetBalance}
            toAmount={toAmount}
            addressError={addressError}
            addressWarning={addressWarning}
            toAssetBalance={toAssetBalance}
            orderExpiration={orderExpiration}
            slippageTolerance={slippageTolerance}
            swapValidationError={swapValidationError}
            toAddressOrUrl={toAddressOrUrl}
            toAddress={toAddress}
            buyAssetOptions={buyAssetOptions}
            selectedSendAsset={selectedSendAsset}
            sendAssetBalance={sendAssetBalance}
            sendAssetOptions={sendAssetOptions}
            swapAssetOptions={swapAssetOptions}
            isFetchingSwapQuote={isFetchingSwapQuote}
            isSwapSubmitDisabled={isSwapButtonDisabled}
            customSlippageTolerance={customSlippageTolerance}
            defaultCurrencies={defaultCurrencies}
            onCustomSlippageToleranceChange={onCustomSlippageToleranceChange}
            onSetBuyAmount={onSetBuyAmount}
            onSetToAddressOrUrl={onSetToAddressOrUrl}
            onSelectExpiration={onSelectExpiration}
            onSelectPresetFromAmount={onSelectPresetAmountFactory('swap')}
            onSelectPresetSendAmount={onSelectPresetAmountFactory('send')}
            onSelectSlippageTolerance={onSelectSlippageTolerance}
            onSetExchangeRate={onSetExchangeRate}
            onSetSendAmount={onSetSendAmount}
            onSetFromAmount={onSetFromAmount}
            onSetToAmount={onSetToAmount}
            onSubmitSwap={onSubmitSwap}
            onSubmitSend={onSubmitSend}
            onSubmitBuy={onSubmitBuy}
            flipSwapAssets={flipSwapAssets}
            onSelectNetwork={onSelectNetwork}
            onSelectAccount={onSelectAccount}
            onToggleOrderType={onToggleOrderType}
            onSelectAsset={onSelectTransactAsset}
            onSelectTab={setSelectedWidgetTab}
            onSwapQuoteRefresh={onSwapQuoteRefresh}
            onSelectSendAsset={onSelectSendAsset}
            onAddNetwork={onAddNetwork}
            onAddAsset={onShowVisibleAssetsModal}
          />
        </WalletWidgetStandIn>
      }
    </WalletPageLayout>
  )
}

function mapStateToProps (state: WalletPageState): Partial<Props> {
  return {
    page: state.page,
    wallet: state.wallet
  }
}

function mapDispatchToProps (dispatch: Dispatch): Partial<Props> {
  return {
    walletPageActions: bindActionCreators(WalletPageActions, store.dispatch.bind(store)),
    walletActions: bindActionCreators(WalletActions, store.dispatch.bind(store))
  }
}

export default connect(mapStateToProps, mapDispatchToProps)(Container)
