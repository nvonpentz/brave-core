/* Copyright 2022 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_BRAVE_WALLET_BROWSER_ASSET_DISCOVERY_SERVICE_H_
#define BRAVE_COMPONENTS_BRAVE_WALLET_BROWSER_ASSET_DISCOVERY_SERVICE_H_

#include <string>
#include <vector>

#include "base/memory/raw_ptr.h"
#include "brave/components/brave_wallet/common/brave_wallet.mojom.h"
#include "mojo/public/cpp/bindings/receiver.h"

namespace brave_wallet {

class BraveWalletService;
class KeyringService;
class JsonRpcService;

// Scans the blockchain for assets the user owns and automatically adds them
class AssetDiscoveryService : public mojom::KeyringServiceObserver {
 public:
  AssetDiscoveryService(BraveWalletService* wallet_service,
                        KeyringService* keyring_service,
                        JsonRpcService* json_rpc_service);

  ~AssetDiscoveryService() override;
  AssetDiscoveryService(const AssetDiscoveryService&) = delete;
  AssetDiscoveryService& operator=(AssetDiscoveryService&) = delete;

  using DiscoverAssetsCallback =
      base::OnceCallback<void(const std::vector<mojom::BlockchainTokenPtr>,
                              mojom::ProviderError error,
                              const std::string& error_message)>;

  void OnAssetsDiscovered(const std::vector<mojom::BlockchainTokenPtr>,
                          mojom::ProviderError error,
                          const std::string& error_message);

  void OnDiscoveredAssetAdded(const bool success);

 private:
  // KeyringServiceObserver
  void KeyringCreated(const std::string& keyring_id) override {}
  void KeyringRestored(const std::string& keyring_id) override {}
  void KeyringReset() override {}
  void Locked() override {}
  void Unlocked() override {}
  void BackedUp() override {}
  void AccountsChanged() override{};
  void AccountsAdded(
      const std::vector<mojom::AccountInfoPtr> account_infos) override;
  void AutoLockMinutesChanged() override {}
  void SelectedAccountChanged(mojom::CoinType coin) override {}

  raw_ptr<BraveWalletService> wallet_service_;
  raw_ptr<KeyringService> keyring_service_;
  raw_ptr<JsonRpcService> json_rpc_service_;
  mojo::Receiver<brave_wallet::mojom::KeyringServiceObserver>
      keyring_service_observer_receiver_{this};

  base::WeakPtrFactory<AssetDiscoveryService> weak_ptr_factory_;
};

}  // namespace brave_wallet

#endif  // BRAVE_COMPONENTS_BRAVE_WALLET_BROWSER_ASSET_DISCOVERY_SERVICE_H_
