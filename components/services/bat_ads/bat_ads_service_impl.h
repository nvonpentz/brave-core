/* Copyright (c) 2019 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_SERVICES_BAT_ADS_BAT_ADS_SERVICE_IMPL_H_
#define BRAVE_COMPONENTS_SERVICES_BAT_ADS_BAT_ADS_SERVICE_IMPL_H_

#include "brave/components/brave_ads/common/interfaces/ads.mojom-forward.h"
#include "brave/components/services/bat_ads/public/interfaces/bat_ads.mojom.h"
#include "mojo/public/cpp/bindings/pending_associated_receiver.h"
#include "mojo/public/cpp/bindings/pending_associated_remote.h"
#include "mojo/public/cpp/bindings/pending_receiver.h"
#include "mojo/public/cpp/bindings/receiver.h"
#include "mojo/public/cpp/bindings/unique_associated_receiver_set.h"

namespace bat_ads {

class BatAdsServiceImpl : public mojom::BatAdsService {
 public:
  explicit BatAdsServiceImpl(
      mojo::PendingReceiver<mojom::BatAdsService> receiver);

  BatAdsServiceImpl(const BatAdsServiceImpl&) = delete;
  BatAdsServiceImpl& operator=(const BatAdsServiceImpl&) = delete;

  BatAdsServiceImpl(BatAdsServiceImpl&& other) noexcept = delete;
  BatAdsServiceImpl& operator=(BatAdsServiceImpl&& other) noexcept = delete;

  ~BatAdsServiceImpl() override;

  // BatAdsService:
  void Create(mojo::PendingAssociatedRemote<mojom::BatAdsClient> bat_ads_client,
              mojo::PendingAssociatedReceiver<mojom::BatAds> bat_ads,
              mojo::PendingReceiver<mojom::BatAdsClientNotifier>
                  bat_ads_client_notifier,
              CreateCallback callback) override;

 private:
  mojo::Receiver<mojom::BatAdsService> receiver_;
  mojo::UniqueAssociatedReceiverSet<mojom::BatAds> associated_receivers_;
};

}  // namespace bat_ads

#endif  // BRAVE_COMPONENTS_SERVICES_BAT_ADS_BAT_ADS_SERVICE_IMPL_H_
