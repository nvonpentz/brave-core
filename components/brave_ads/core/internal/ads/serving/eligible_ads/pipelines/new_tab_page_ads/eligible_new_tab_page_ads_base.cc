/* Copyright (c) 2021 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/brave_ads/core/internal/ads/serving/eligible_ads/pipelines/new_tab_page_ads/eligible_new_tab_page_ads_base.h"

#include "brave/components/brave_ads/core/internal/geographic/subdivision/subdivision_targeting.h"
#include "brave/components/brave_ads/core/internal/resources/behavioral/anti_targeting/anti_targeting_resource.h"

namespace brave_ads::new_tab_page_ads {

EligibleAdsBase::EligibleAdsBase(
    const SubdivisionTargeting& subdivision_targeting,
    const resource::AntiTargeting& anti_targeting_resource)
    : subdivision_targeting_(subdivision_targeting),
      anti_targeting_resource_(anti_targeting_resource) {}

EligibleAdsBase::~EligibleAdsBase() = default;

}  // namespace brave_ads::new_tab_page_ads
