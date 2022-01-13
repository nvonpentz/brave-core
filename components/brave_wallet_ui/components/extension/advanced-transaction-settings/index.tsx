import * as React from 'react'

import { getLocale } from '../../../../common/locale'
import { NavButton, Panel } from '../'

// Styled Components
import {
  StyledWrapper,
  FormColumn,
  Input,
  InputLabel,
  ButtonRow
} from './style'

// Utils
import { toHex } from '../../../utils/format-balances'

export interface Props {
  onCancel: () => void
  nonce: string
  txMetaId: string
  updateUnapprovedTransactionNonce: (payload: any) => void
}

const AdvancedTransactionSettings = (props: Props) => {
  const {
    onCancel,
    nonce,
    txMetaId,
    updateUnapprovedTransactionNonce
  } = props
  const [customNonce, setCustomNonce] = React.useState<string>(nonce)

  const handleNonceInputChanged = (event: React.ChangeEvent<HTMLInputElement>) => {
    setCustomNonce(event.target.value)
  }

  const isSaveButtonDisabled = React.useMemo(() => {
    return customNonce === ''
  }, [customNonce])

  const onSave = () => {
    updateUnapprovedTransactionNonce({
      txMetaId,
      nonce: toHex(customNonce)
    })
    onCancel()
  }

  return (
    <Panel
      navAction={onCancel}
      title={getLocale('braveWalletAdvancedTransactionSettings')}
    >
      <StyledWrapper>
        <FormColumn>
          <InputLabel>{getLocale('braveWalletEditNonce')}</InputLabel>
          <Input
            placeholder='0'
            type='number'
            value={customNonce}
            onChange={handleNonceInputChanged}
          />
        </FormColumn>
        <ButtonRow>
          <NavButton
            buttonType='secondary'
            needsTopMargin={true}
            text={getLocale('braveWalletBackupButtonCancel')}
            onSubmit={onCancel}
          />
          <NavButton
            buttonType='primary'
            text={getLocale('braveWalletAccountSettingsSave')}
            onSubmit={onSave}
            disabled={isSaveButtonDisabled}
          />
        </ButtonRow>
      </StyledWrapper>
    </Panel>
  )
}

export default AdvancedTransactionSettings
