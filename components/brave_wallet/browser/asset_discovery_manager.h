/* Copyright (c) 2022 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_BRAVE_WALLET_BROWSER_ASSET_DISCOVERY_MANAGER_H_
#define BRAVE_COMPONENTS_BRAVE_WALLET_BROWSER_ASSET_DISCOVERY_MANAGER_H_

#include <map>
#include <string>
#include <utility>
#include <vector>

#include "base/barrier_callback.h"
#include "base/memory/raw_ptr.h"
#include "base/memory/weak_ptr.h"
#include "brave/components/api_request_helper/api_request_helper.h"
#include "brave/components/brave_wallet/browser/blockchain_list_parser.h"
#include "brave/components/brave_wallet/common/brave_wallet.mojom.h"
#include "brave/components/brave_wallet/common/brave_wallet_types.h"
#include "brave/components/brave_wallet/common/solana_address.h"
#include "mojo/public/cpp/bindings/receiver.h"

class PrefService;

namespace brave_wallet {

class BraveWalletService;
class JsonRpcService;
class KeyringService;

class AssetDiscoveryManager : public mojom::KeyringServiceObserver {
 public:
  AssetDiscoveryManager(BraveWalletService* wallet_service,
                        JsonRpcService* json_rpc_service,
                        KeyringService* keyring_service,
                        PrefService* prefs);

  AssetDiscoveryManager(const AssetDiscoveryManager&) = delete;
  AssetDiscoveryManager& operator=(AssetDiscoveryManager&) = delete;
  ~AssetDiscoveryManager() override;

  // KeyringServiceObserver
  void KeyringCreated(const std::string& keyring_id) override {}
  void KeyringRestored(const std::string& keyring_id) override {}
  void KeyringReset() override {}
  void Locked() override {}
  void Unlocked() override {}
  void BackedUp() override {}
  void AccountsChanged() override {}
  void AccountsAdded(mojom::CoinType coin,
                     const std::vector<std::string>& addresses) override;
  void AutoLockMinutesChanged() override {}
  void SelectedAccountChanged(mojom::CoinType coin) override {}

  using APIRequestResult = api_request_helper::APIRequestResult;
  using DiscoverAssetsCompletedCallbackForTesting =
      base::RepeatingCallback<void(
          std::vector<mojom::BlockchainTokenPtr> discovered_assets_for_chain)>;

  // Called by frontend via BraveWalletService.
  // Subject to client side rate limiting based on
  // kBraveWalletLastDiscoveredAssetsAt pref value.
  // kBraveWalletNextAssetDiscoveryFromBlocks pref and "latest" for ETH chains.
  void DiscoverAssetsOnAllSupportedChainsRefresh(
      std::map<mojom::CoinType, std::vector<std::string>>& account_addresses);

  void SetSupportedChainsForTesting(
      const std::vector<std::string> supported_chains_for_testing) {
    supported_chains_for_testing_ = supported_chains_for_testing;
  }

  void SetDiscoverAssetsCompletedCallbackForTesting(
      DiscoverAssetsCompletedCallbackForTesting callback) {
    discover_assets_completed_callback_for_testing_ = std::move(callback);
  }

 private:
  friend class AssetDiscoveryManagerUnitTest;
  FRIEND_TEST_ALL_PREFIXES(AssetDiscoveryManagerUnitTest,
                           DiscoverAssetsOnAllSupportedChainsRefresh);

  const std::vector<std::string>& GetAssetDiscoverySupportedEthChains();

  void DiscoverSolAssets(const std::vector<std::string>& account_addresses,
                         bool triggered_by_accounts_added);

  void OnGetSolanaTokenAccountsByOwner(
      base::OnceCallback<void(std::vector<SolanaAddress>)> barrier_callback,
      const std::vector<SolanaAccountInfo>& token_accounts,
      mojom::SolanaProviderError error,
      const std::string& error_message);

  void MergeDiscoveredSolanaAssets(
      bool triggered_by_accounts_added,
      const std::vector<std::vector<SolanaAddress>>&
          all_discovered_contract_addresses);

  void OnGetSolanaTokenRegistry(
      bool triggered_by_accounts_added,
      const base::flat_set<std::string>& discovered_contract_addresses,
      std::vector<mojom::BlockchainTokenPtr> sol_token_registry);

  void DiscoverEthAssets(const std::vector<std::string>& account_addresses,
                         bool triggered_by_accounts_added);

  void OnGetERC20TokenBalances(
      base::OnceCallback<void(std::map<std::string, std::vector<std::string>>)>
          barrier_callback,
      const std::string& chain_id,
      bool triggered_by_accounts_added,
      const std::vector<std::string>& contract_addresses,
      std::vector<mojom::ERC20BalanceResultPtr> balance_results,
      mojom::ProviderError error,
      const std::string& error_message);

  void MergeDiscoveredEthAssets(
      base::flat_map<std::string,
                     base::flat_map<std::string, mojom::BlockchainTokenPtr>>
          chain_id_to_contract_address_to_token,
      bool triggered_by_accounts_added,
      const std::vector<std::map<std::string, std::vector<std::string>>>
          discovered_assets);

  // CompleteDiscoverAssets signals that the discover assets request has
  // completed for a given chain_id
  void CompleteDiscoverAssets(
      const std::string& chain_id,
      std::vector<mojom::BlockchainTokenPtr> discovered_assets,
      absl::optional<mojom::ProviderError> error,
      const std::string& error_message,
      bool triggered_by_accounts_added);

  // CompleteDiscoverAssets signals that the discover assets request has
  // completed for a given chain_id x account_address combination.
  void CompleteDiscoverAssets(
      std::vector<mojom::BlockchainTokenPtr> discovered_assets,
      bool triggered_by_accounts_added);

  // Triggered by when KeyringService emits AccountsAdded event.
  // Rate limits will be ignored.
  void DiscoverAssetsOnAllSupportedChainsAccountsAdded(
      mojom::CoinType coin,
      const std::vector<std::string>& account_addresses);

  friend class AssetDiscoveryManagerUnitTest;
  FRIEND_TEST_ALL_PREFIXES(AssetDiscoveryManagerUnitTest, DecodeMintAddress);

  static absl::optional<SolanaAddress> DecodeMintAddress(
      const std::vector<uint8_t>& data);

  // remaining_buckets_ is the number of 'buckets' of requets remaining for an
  // in-flight DiscoverAssetsOnAllSupportedChainsRefresh call to be completed.
  // When no call is in-flight, remaining_buckets_ is 0.  When a call is
  // in-flight, remaining_chains_ is > 0 and the AssetDiscoverManager will
  // refuse to process additional  DiscoverAssetsOnAllSupportedChainsRefresh
  // calls.
  //
  // DiscoverAssetsOnAllSupportedChainsAccountsAdded does not read from or write
  // to remaining_chains_ and thus those calls will always processed.
  int remaining_buckets_ = 0;
  std::vector<mojom::BlockchainTokenPtr> discovered_assets_;
  std::vector<std::string> supported_chains_for_testing_;
  DiscoverAssetsCompletedCallbackForTesting
      discover_assets_completed_callback_for_testing_;
  raw_ptr<BraveWalletService> wallet_service_;
  raw_ptr<JsonRpcService> json_rpc_service_;
  raw_ptr<KeyringService> keyring_service_;
  raw_ptr<PrefService> prefs_;
  mojo::Receiver<brave_wallet::mojom::KeyringServiceObserver>
      keyring_service_observer_receiver_{this};
  base::WeakPtrFactory<AssetDiscoveryManager> weak_ptr_factory_;
};

}  // namespace brave_wallet

#endif  // BRAVE_COMPONENTS_BRAVE_WALLET_BROWSER_ASSET_DISCOVERY_MANAGER_H_
