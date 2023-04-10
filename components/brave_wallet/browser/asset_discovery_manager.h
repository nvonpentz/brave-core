/* Copyright (c) 2022 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_BRAVE_WALLET_BROWSER_ASSET_DISCOVERY_MANAGER_H_
#define BRAVE_COMPONENTS_BRAVE_WALLET_BROWSER_ASSET_DISCOVERY_MANAGER_H_

#include <map>
#include <memory>
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

class AssetDiscoveryTask {
 public:
  using APIRequestHelper = api_request_helper::APIRequestHelper;
  using APIRequestResult = api_request_helper::APIRequestResult;

  AssetDiscoveryTask(APIRequestHelper* api_request_helper,
                     BraveWalletService* wallet_service,
                     JsonRpcService* json_rpc_service,
                     PrefService* prefs);

  AssetDiscoveryTask(const AssetDiscoveryTask&) = delete;
  AssetDiscoveryTask& operator=(AssetDiscoveryTask&) = delete;
  ~AssetDiscoveryTask();

  void DiscoverAssets(
      const std::map<mojom::CoinType, std::vector<std::string>>& chain_ids,
      const std::map<mojom::CoinType, std::vector<std::string>>&
          account_addresses,
      base::OnceClosure callback);

 private:
  friend class AssetDiscoveryTaskUnitTest;
  FRIEND_TEST_ALL_PREFIXES(AssetDiscoveryTaskUnitTest, DecodeMintAddress);
  FRIEND_TEST_ALL_PREFIXES(AssetDiscoveryTaskUnitTest,
                           GetSimpleHashNftsByWalletUrl);
  FRIEND_TEST_ALL_PREFIXES(AssetDiscoveryTaskUnitTest, ParseNFTsFromSimpleHash);

  void MergeDiscoveredAssets(
      base::OnceClosure callback,
      const std::vector<std::vector<mojom::BlockchainTokenPtr>>&
          discovered_assets);

  using DiscoverAssetsCompletedCallback =
      base::OnceCallback<void(std::vector<mojom::BlockchainTokenPtr> nfts)>;

  void DiscoverERC20sFromRegistry(
      const std::vector<std::string>& chain_ids,
      const std::vector<std::string>& account_addresses,
      DiscoverAssetsCompletedCallback callback);
  void OnGetERC20TokenBalances(
      base::OnceCallback<void(std::map<std::string, std::vector<std::string>>)>
          barrier_callback,
      const std::string& chain_id,
      const std::vector<std::string>& contract_addresses,
      std::vector<mojom::ERC20BalanceResultPtr> balance_results,
      mojom::ProviderError error,
      const std::string& error_message);
  void MergeDiscoveredERC20s(
      base::flat_map<std::string,
                     base::flat_map<std::string, mojom::BlockchainTokenPtr>>
          chain_id_to_contract_address_to_token,
      DiscoverAssetsCompletedCallback callback,
      const std::vector<std::map<std::string, std::vector<std::string>>>&
          discovered_assets);

  void DiscoverSPLTokensFromRegistry(
      const std::vector<std::string>& account_addresses,
      DiscoverAssetsCompletedCallback callback);
  void OnGetSolanaTokenAccountsByOwner(
      base::OnceCallback<void(std::vector<SolanaAddress>)> barrier_callback,
      const std::vector<SolanaAccountInfo>& token_accounts,
      mojom::SolanaProviderError error,
      const std::string& error_message);
  void MergeDiscoveredSPLTokens(DiscoverAssetsCompletedCallback callback,
                                const std::vector<std::vector<SolanaAddress>>&
                                    all_discovered_contract_addresses);
  void OnGetSolanaTokenRegistry(
      DiscoverAssetsCompletedCallback callback,
      const base::flat_set<std::string>& discovered_contract_addresses,
      std::vector<mojom::BlockchainTokenPtr> sol_token_registry);

  // For discovering NFTs on Solana and Ethereum
  using FetchNFTsFromSimpleHashCallback =
      base::OnceCallback<void(std::vector<mojom::BlockchainTokenPtr> nfts)>;
  void FetchNFTsFromSimpleHash(const std::string& account_address,
                               const std::vector<std::string>& chain_ids,
                               mojom::CoinType coin,
                               FetchNFTsFromSimpleHashCallback callback);
  void OnFetchNFTsFromSimpleHash(
      std::vector<mojom::BlockchainTokenPtr> nfts_so_far,
      mojom::CoinType coin,
      FetchNFTsFromSimpleHashCallback callback,
      APIRequestResult api_request_result);
  void DiscoverNFTs(
      const std::map<mojom::CoinType, std::vector<std::string>>& chain_ids,
      const std::map<mojom::CoinType, std::vector<std::string>>&
          account_addresses,
      DiscoverAssetsCompletedCallback callback);
  void MergeDiscoveredNFTs(
      DiscoverAssetsCompletedCallback callback,
      const std::vector<std::vector<mojom::BlockchainTokenPtr>>& nfts);

  absl::optional<std::pair<GURL, std::vector<mojom::BlockchainTokenPtr>>>
  ParseNFTsFromSimpleHash(const base::Value& json_value, mojom::CoinType coin);

  static absl::optional<SolanaAddress> DecodeMintAddress(
      const std::vector<uint8_t>& data);
  static GURL GetSimpleHashNftsByWalletUrl(
      const std::string& account_address,
      const std::vector<std::string>& chain_ids);

  raw_ptr<APIRequestHelper> api_request_helper_;
  raw_ptr<BraveWalletService> wallet_service_;
  raw_ptr<JsonRpcService> json_rpc_service_;
  raw_ptr<PrefService> prefs_;
  base::WeakPtrFactory<AssetDiscoveryTask> weak_ptr_factory_;
};

class AssetDiscoveryManager : public mojom::KeyringServiceObserver {
 public:
  using APIRequestHelper = api_request_helper::APIRequestHelper;
  using APIRequestResult = api_request_helper::APIRequestResult;
  AssetDiscoveryManager(
      scoped_refptr<network::SharedURLLoaderFactory> url_loader_factory,
      BraveWalletService* wallet_service,
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

  // Called by frontend via BraveWalletService and when new accounts are added
  // via the KeyringServiceObserver implementation
  void DiscoverAssetsOnAllSupportedChains(
      const std::map<mojom::CoinType, std::vector<std::string>>&
          account_addresses,
      bool triggered_by_accounts_added);

  void SetQueueSizeForTesting(int queue_size) { queue_size_ = queue_size; }

 private:
  friend class AssetDiscoveryManagerUnitTest;
  FRIEND_TEST_ALL_PREFIXES(AssetDiscoveryManagerUnitTest,
                           GetAssetDiscoverySupportedChains);

  const std::map<mojom::CoinType, std::vector<std::string>>&
  GetAssetDiscoverySupportedChains();

  void ScheduleTask(const std::map<mojom::CoinType, std::vector<std::string>>&
                        account_addresses);
  void StartTask(const std::map<mojom::CoinType, std::vector<std::string>>&
                     account_addresses);
  void FinishTask();

  // If queue_size_ is greater than zero new tasks will not be scheduled
  // unless triggered by accounts being added
  int queue_size_ = 0;
  std::unique_ptr<APIRequestHelper> api_request_helper_;
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
