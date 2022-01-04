import * as React from 'react'

import {
  WalletAccountType,
  AccountSettingsNavTypes,
  UpdateAccountNamePayloadType,
  AccountTransactions,
  BraveWallet,
  DefaultCurrencies
} from '../../../../constants/types'
import { reduceAddress } from '../../../../utils/reduce-address'
import { copyToClipboard } from '../../../../utils/copy-to-clipboard'
import { create } from 'ethereum-blockies'
import { getLocale } from '../../../../../common/locale'
import { formatBalance } from '../../../../utils/format-balances'
import { sortTransactionByDate } from '../../../../utils/tx-utils'

// Styled Components
import {
  StyledWrapper,
  SectionTitle,
  PrimaryListContainer,
  PrimaryRow,
  SecondaryListContainer,
  DisclaimerText,
  SubDivider,
  BackupIcon,
  ButtonsRow,
  SettingsIcon,
  BackupButton,
  BackupButtonText,
  Button,
  TopRow,
  WalletInfoRow,
  WalletAddress,
  WalletName,
  AccountCircle,
  WalletInfoLeftSide,
  QRCodeIcon,
  EditIcon,
  SubviewSectionTitle,
  TransactionPlaceholderContainer
} from './style'

import { TransactionPlaceholderText, Spacer } from '../portfolio/style'

// Components
import { BackButton, Tooltip } from '../../../shared'
import {
  AccountListItem,
  AddButton,
  PortfolioAssetItem,
  AccountSettingsModal,
  PortfolioTransactionItem
} from '../../'

export interface Props {
  accounts: WalletAccountType[]
  transactions: AccountTransactions
  privateKey: string
  selectedNetwork: BraveWallet.EthereumChain
  userVisibleTokensInfo: BraveWallet.ERCToken[]
  transactionSpotPrices: BraveWallet.AssetPrice[]
  selectedAccount: WalletAccountType | undefined
  defaultCurrencies: DefaultCurrencies
  onViewPrivateKey: (address: string, isDefault: boolean) => void
  onDoneViewingPrivateKey: () => void
  toggleNav: () => void
  onClickBackup: () => void
  onClickAddAccount: () => void
  onUpdateAccountName: (payload: UpdateAccountNamePayloadType) => { success: boolean }
  onRemoveAccount: (address: string, hardware: boolean) => void
  onSelectAccount: (account: WalletAccountType) => void
  onSelectAsset: (token: BraveWallet.ERCToken) => void
  goBack: () => void
  onRetryTransaction: (transaction: BraveWallet.TransactionInfo) => void
  onSpeedupTransaction: (transaction: BraveWallet.TransactionInfo) => void
  onCancelTransaction: (transaction: BraveWallet.TransactionInfo) => void
}

function Accounts (props: Props) {
  const {
    accounts,
    transactions,
    privateKey,
    selectedNetwork,
    transactionSpotPrices,
    userVisibleTokensInfo,
    selectedAccount,
    defaultCurrencies,
    goBack,
    onSelectAccount,
    onSelectAsset,
    onViewPrivateKey,
    onDoneViewingPrivateKey,
    toggleNav,
    onClickBackup,
    onClickAddAccount,
    onUpdateAccountName,
    onRemoveAccount,
    onRetryTransaction,
    onSpeedupTransaction,
    onCancelTransaction
  } = props

  const groupById = (accounts: WalletAccountType[], key: string) => {
    return accounts.reduce((result, obj) => {
      (result[obj[key]] = result[obj[key]] || []).push(obj)
      return result
    }, {})
  }

  const primaryAccounts = React.useMemo(() => {
    return accounts.filter((account) => account.accountType === 'Primary')
  }, [accounts])

  const secondaryAccounts = React.useMemo(() => {
    return accounts.filter((account) => account.accountType === 'Secondary')
  }, [accounts])

  const trezorAccounts = React.useMemo(() => {
    const foundTrezorAccounts = accounts.filter((account) => account.accountType === 'Trezor')
    return groupById(foundTrezorAccounts, 'deviceId')
  }, [accounts])

  const ledgerAccounts = React.useMemo(() => {
    const foundLedgerAccounts = accounts.filter((account) => account.accountType === 'Ledger')
    return groupById(foundLedgerAccounts, 'deviceId')
  }, [accounts])

  const [showEditModal, setShowEditModal] = React.useState<boolean>(false)
  const [editTab, setEditTab] = React.useState<AccountSettingsNavTypes>('details')

  const sortAccountsByName = React.useCallback((accounts: WalletAccountType[]) => {
    return [...accounts].sort(function (a: WalletAccountType, b: WalletAccountType) {
      if (a.name < b.name) {
        return -1
      }

      if (a.name > b.name) {
        return 1
      }

      return 0
    })
  }, [])

  const onCopyToClipboard = async () => {
    if (selectedAccount) {
      await copyToClipboard(selectedAccount.address)
    }
  }

  const onClickSettings = () => {
    chrome.tabs.create({ url: 'chrome://settings/wallet' }, () => {
      if (chrome.runtime.lastError) {
        console.error('tabs.create failed: ' + chrome.runtime.lastError.message)
      }
    })
  }

  const onChangeTab = (id: AccountSettingsNavTypes) => {
    setEditTab(id)
  }

  const onShowEditModal = () => {
    setShowEditModal(!showEditModal)
  }

  const onCloseEditModal = () => {
    setShowEditModal(!showEditModal)
    setEditTab('details')
  }

  const orb = React.useMemo(() => {
    if (selectedAccount) {
      return create({ seed: selectedAccount.address.toLowerCase(), size: 8, scale: 16 }).toDataURL()
    }
  }, [selectedAccount])

  const transactionList = React.useMemo(() => {
    if (selectedAccount?.address && transactions[selectedAccount.address]) {
      return sortTransactionByDate(transactions[selectedAccount.address], 'descending')
    } else {
      return []
    }
  }, [selectedAccount, transactions])

  const erc271Tokens = React.useMemo(() => selectedAccount?.tokens.filter((token) => token.asset.isErc721), [selectedAccount])

  return (
    <StyledWrapper>
      {selectedAccount &&
        <TopRow><BackButton onSubmit={goBack} /></TopRow>
      }
      {!selectedAccount ? (
        <>
          <PrimaryRow>
            <SectionTitle>{getLocale('braveWalletAccountsPrimary')}</SectionTitle>
            <ButtonsRow>
              <BackupButton onClick={onClickBackup}>
                <BackupIcon />
                <BackupButtonText>{getLocale('braveWalletBackupButton')}</BackupButtonText>
              </BackupButton>
              <Button onClick={onClickSettings}>
                <SettingsIcon />
              </Button>
            </ButtonsRow>
          </PrimaryRow>
          <PrimaryListContainer>
            {primaryAccounts.map((account) =>
              <AccountListItem
                key={account.id}
                isHardwareWallet={false}
                onClick={onSelectAccount}
                onRemoveAccount={onRemoveAccount}
                account={account}
              />
            )}
          </PrimaryListContainer>
          <SectionTitle>{getLocale('braveWalletAccountsSecondary')}</SectionTitle>
          <DisclaimerText>{getLocale('braveWalletAccountsSecondaryDisclaimer')}</DisclaimerText>
          <SubDivider />
          <SecondaryListContainer isHardwareWallet={false}>
            {secondaryAccounts.map((account) =>
              <AccountListItem
                key={account.id}
                isHardwareWallet={false}
                onClick={onSelectAccount}
                onRemoveAccount={onRemoveAccount}
                account={account}
              />
            )}
          </SecondaryListContainer>
          {Object.keys(trezorAccounts).map(key =>
            <SecondaryListContainer key={key} isHardwareWallet={true}>
              {sortAccountsByName(trezorAccounts[key]).map((account: WalletAccountType) =>
                <AccountListItem
                  key={account.id}
                  isHardwareWallet={true}
                  onClick={onSelectAccount}
                  onRemoveAccount={onRemoveAccount}
                  account={account}
                />
              )}
            </SecondaryListContainer>
          )}
          {Object.keys(ledgerAccounts).map(key =>
            <SecondaryListContainer key={key} isHardwareWallet={true}>
              {sortAccountsByName(ledgerAccounts[key]).map((account: WalletAccountType) =>
                <AccountListItem
                  key={account.id}
                  isHardwareWallet={true}
                  onClick={onSelectAccount}
                  onRemoveAccount={onRemoveAccount}
                  account={account}
                />
              )}
            </SecondaryListContainer>
          )}
          <AddButton
            buttonType='secondary'
            onSubmit={onClickAddAccount}
            text={getLocale('braveWalletAddAccount')}
          />
        </>
      ) : (
        <>
          <WalletInfoRow>
            <WalletInfoLeftSide>
              <AccountCircle orb={orb} />
              <WalletName>{selectedAccount.name}</WalletName>
              <Tooltip text={getLocale('braveWalletToolTipCopyToClipboard')}>
                <WalletAddress onClick={onCopyToClipboard}>{reduceAddress(selectedAccount.address)}</WalletAddress>
              </Tooltip>
              <Button onClick={onShowEditModal}>
                <QRCodeIcon />
              </Button>
            </WalletInfoLeftSide>
            <Button onClick={onShowEditModal}>
              <EditIcon />
            </Button>
          </WalletInfoRow>
          <SubviewSectionTitle>{getLocale('braveWalletAccountsAssets')}</SubviewSectionTitle>
          <SubDivider />
          {selectedAccount.tokens.filter((token) => !token.asset.isErc721).map((item) =>
            <PortfolioAssetItem
              defaultCurrencies={defaultCurrencies}
              key={item.asset.contractAddress}
              assetBalance={formatBalance(item.assetBalance, item.asset.decimals)}
              fiatBalance={item.fiatBalance}
              token={item.asset}
            />
          )}
          {erc271Tokens?.length !== 0 &&
            <>
              <Spacer />
              <SubviewSectionTitle>{getLocale('braveWalletTopNavNFTS')}</SubviewSectionTitle>
              <SubDivider />
              {erc271Tokens?.map((item) =>
                <PortfolioAssetItem
                  defaultCurrencies={defaultCurrencies}
                  key={item.asset.contractAddress}
                  assetBalance={formatBalance(item.assetBalance, item.asset.decimals)}
                  fiatBalance={item.fiatBalance}
                  token={item.asset}
                />
              )}
              <Spacer />
            </>
          }
          <SubviewSectionTitle>{getLocale('braveWalletTransactions')}</SubviewSectionTitle>
          <SubDivider />
          {transactionList.length !== 0 ? (
            <>
              {transactionList.map((transaction) =>
                <PortfolioTransactionItem
                  defaultCurrencies={defaultCurrencies}
                  selectedNetwork={selectedNetwork}
                  key={transaction?.id}
                  transaction={transaction}
                  account={selectedAccount}
                  accounts={accounts}
                  transactionSpotPrices={transactionSpotPrices}
                  visibleTokens={userVisibleTokensInfo}
                  displayAccountName={false}
                  onSelectAccount={onSelectAccount}
                  onSelectAsset={onSelectAsset}
                  onRetryTransaction={onRetryTransaction}
                  onSpeedupTransaction={onSpeedupTransaction}
                  onCancelTransaction={onCancelTransaction}
                />
              )}
            </>
          ) : (
            <TransactionPlaceholderContainer>
              <TransactionPlaceholderText>{getLocale('braveWalletTransactionPlaceholder')}</TransactionPlaceholderText>
            </TransactionPlaceholderContainer>
          )}
        </>
      )}
      {showEditModal && selectedAccount &&
        <AccountSettingsModal
          title={getLocale('braveWalletAccount')}
          account={selectedAccount}
          onClose={onCloseEditModal}
          onUpdateAccountName={onUpdateAccountName}
          onCopyToClipboard={onCopyToClipboard}
          onChangeTab={onChangeTab}
          onToggleNav={toggleNav}
          onRemoveAccount={onRemoveAccount}
          onViewPrivateKey={onViewPrivateKey}
          onDoneViewingPrivateKey={onDoneViewingPrivateKey}
          privateKey={privateKey}
          tab={editTab}
          hideNav={false}
        />
      }
    </StyledWrapper>
  )
}

export default Accounts
