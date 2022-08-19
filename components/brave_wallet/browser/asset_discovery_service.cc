/* Copyright 2022 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "brave/components/brave_wallet/browser/asset_discovery_service.h"

#include <utility>

#include "base/metrics/histogram_macros.h"
#include "brave/components/brave_wallet/browser/brave_wallet_service.h"
#include "brave/components/brave_wallet/browser/json_rpc_service.h"
#include "brave/components/brave_wallet/browser/keyring_service.h"
#include "brave/components/brave_wallet/common/brave_wallet.mojom.h"

namespace brave_wallet {

AssetDiscoveryService::AssetDiscoveryService(BraveWalletService* wallet_service,
                                             KeyringService* keyring_service,
                                             JsonRpcService* json_rpc_service)
    : wallet_service_(wallet_service),
      keyring_service_(keyring_service),
      json_rpc_service_(json_rpc_service),
      weak_ptr_factory_(this) {
  DCHECK(wallet_service_);
  DCHECK(keyring_service_);
  DCHECK(json_rpc_service_);
  keyring_service_->AddObserver(
      keyring_service_observer_receiver_.BindNewPipeAndPassRemote());
}

AssetDiscoveryService::~AssetDiscoveryService() = default;

// KeyringServiceObserver
void AssetDiscoveryService::AccountsAdded(
    const std::vector<mojom::AccountInfoPtr> account_infos) {
  std::vector<std::string> addresses;
  for (const auto& account_info : account_infos) {
    // Asset discovery only supported on Mainnet Ethereum
    if (account_info->coin == mojom::CoinType::ETH) {
      addresses.push_back(std::move(account_info->address));
    }
  }

  if (addresses.size() == 0u) {
    return;
  }

  auto internal_callback =
      base::BindOnce(&AssetDiscoveryService::OnAssetsDiscovered,
                     weak_ptr_factory_.GetWeakPtr());
  json_rpc_service_->DiscoverAssets(mojom::kMainnetChainId, addresses,
                                    std::move(internal_callback));
}

void AssetDiscoveryService::OnAssetsDiscovered(
    const std::vector<mojom::BlockchainTokenPtr> discovered_tokens,
    mojom::ProviderError error,
    const std::string& error_message) {
  if (error != mojom::ProviderError::kSuccess) {
    VLOG(1) << __func__
            << " encountered ProviderError fetching discovered assets: "
            << error_message;
    return;
  }

  if (discovered_tokens.size() == 0) {
    return;
  }

  for (const auto& token : discovered_tokens) {
    wallet_service_->AddUserAsset(
        token.Clone(),  // TODO(nvonpentz) - figure out why I need to clone
                        // and/or stop cloning
        base::BindOnce(&AssetDiscoveryService::OnDiscoveredAssetAdded,
                       weak_ptr_factory_.GetWeakPtr()));
  }
}

void AssetDiscoveryService::OnDiscoveredAssetAdded(const bool success) {
  if (!success) {
    VLOG(1) << "Unable to add discovered user asset.";
  }
}

}  // namespace brave_wallet
