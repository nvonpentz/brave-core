/* Copyright 2022 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "brave/components/brave_wallet/browser/asset_discovery_service.h"

#include "base/metrics/histogram_macros.h"
#include "brave/components/brave_wallet/browser/brave_wallet_service.h"
#include "brave/components/brave_wallet/browser/keyring_service.h"

// #include "brave/components/brave_wallet/browser/pref_names.h"
// #include "components/prefs/pref_service.h"

namespace brave_wallet {

AssetDiscoveryService::AssetDiscoveryService(BraveWalletService* wallet_service,
                                             KeyringService* keyring_service)
    // PrefService* pref_service)
    : wallet_service_(wallet_service), keyring_service_(keyring_service) {
  // pref_service_(pref_service) {
  // RecordInitialAssetDiscoveryServiceState();
  // wallet_service_->AddObserver(
  //     wallet_service_observer_receiver_.BindNewPipeAndPassRemote());
  keyring_service_->AddObserver(
      keyring_service_observer_receiver_.BindNewPipeAndPassRemote());
}

AssetDiscoveryService::~AssetDiscoveryService() = default;

// KeyringServiceObserver
void AssetDiscoveryService::AccountsChanged() {
  return;
  // KeyringService::GetAccountInfosForKeyring
  // JsonRpcService::DiscoverAssets
  // loop BraveWalletService::AddUserAsset
  // BraveWalletService::GetUserAssets
}

void AssetDiscoveryService::AccountsAdded(
    const std::vector<mojom::AccountInfoPtr> account_infos) {
  return;
}

void AssetDiscoveryService::AccountsRemoved(const std::vector<std::string>&) {
  return;
}

}  // namespace brave_wallet
