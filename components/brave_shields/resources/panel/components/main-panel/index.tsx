import * as React from 'react'

import * as S from './style'
import { FabulouslyLargeToggle } from '../../../../../web-components/toggle'
import AdvancedControlsContent from '../advanced-controls-content'
import { getLocale, splitStringForTag } from '../../../../../common/locale'

function MainPanel () {
  const [isExpanded, setIsExpanded] = React.useState(false)
  const braveShieldsUp = splitStringForTag(getLocale('braveShieldsUp'))
  const braveShieldsBlockedNote = splitStringForTag(getLocale('braveShieldsBlockedNote'))

  return (
    <S.Box>
      <S.PanelHeader>
        <S.SiteTitle>brave.com</S.SiteTitle>
      </S.PanelHeader>
      <S.ToggleBox>
        <FabulouslyLargeToggle
          brand='shields'
          accessibleLabel={getLocale('braveShieldsEnable')}
        />
        <S.StatusText>
          {braveShieldsUp.beforeTag}
          <span>{braveShieldsUp.duringTag}</span>
          {braveShieldsUp.afterTag}
        </S.StatusText>
        <S.BlockCountBox>
          <S.BlockCount>21</S.BlockCount>
          <S.BlockNote>
            {braveShieldsBlockedNote.beforeTag}
            <a href="#">{braveShieldsBlockedNote.duringTag}</a>
            {braveShieldsBlockedNote.afterTag}
          </S.BlockNote>
        </S.BlockCountBox>
        <S.Footnote>
          {getLocale('braveShieldsBroken')}
        </S.Footnote>
      </S.ToggleBox>
      <S.AdvancedControlsButton
        aria-expanded={isExpanded}
        aria-controls='advanced-controls-content'
        onClick={() => setIsExpanded(x => !x)}
      >
        {getLocale('braveShieldsAdvancedCtrls')}
        <S.CaratIcon isExpanded={isExpanded} />
      </S.AdvancedControlsButton>
      {isExpanded && <AdvancedControlsContent />}
    </S.Box>
  )
}

export default MainPanel
