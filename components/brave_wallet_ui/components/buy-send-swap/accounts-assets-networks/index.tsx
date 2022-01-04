import * as React from 'react'
import {
  AccountAssetOptionType,
  BraveWallet,
  BuySendSwapViewTypes,
  UserAccountType
} from '../../../constants/types'

import {
  SelectAccount,
  SelectNetwork,
  SelectAsset
} from '../'

// Styled Components
import {
  StyledWrapper
} from './style'

export interface Props {
  selectedView: BuySendSwapViewTypes
  accounts: UserAccountType[]
  networkList: BraveWallet.EthereumChain[]
  assetOptions: AccountAssetOptionType[]
  selectedNetwork: BraveWallet.EthereumChain
  onAddAsset: () => void
  onClickSelectAccount: (account: UserAccountType) => () => void
  onClickSelectNetwork: (network: BraveWallet.EthereumChain) => () => void
  onSelectedAsset: (account: AccountAssetOptionType) => () => void
  goBack: () => void
  onAddNetwork: () => void
}

function SelectHeader (props: Props) {
  const {
    selectedView,
    accounts,
    networkList,
    assetOptions,
    selectedNetwork,
    onAddAsset,
    onClickSelectAccount,
    goBack,
    onSelectedAsset,
    onClickSelectNetwork,
    onAddNetwork
  } = props

  return (
    <StyledWrapper>
      {selectedView === 'acounts' &&
        <SelectAccount
          accounts={accounts}
          onSelectAccount={onClickSelectAccount}
          onBack={goBack}
        />
      }
      {selectedView === 'assets' &&
        <SelectAsset
          onAddAsset={onAddAsset}
          assets={assetOptions}
          onSelectAsset={onSelectedAsset}
          onBack={goBack}
        />
      }
      {selectedView === 'networks' &&
        <SelectNetwork
          selectedNetwork={selectedNetwork}
          networks={networkList}
          onSelectNetwork={onClickSelectNetwork}
          onBack={goBack}
          hasAddButton={true}
          onAddNetwork={onAddNetwork}
        />
      }
    </StyledWrapper>
  )
}

export default SelectHeader
