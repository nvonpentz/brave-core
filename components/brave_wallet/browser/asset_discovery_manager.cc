/* Copyright 2022 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "brave/components/brave_wallet/browser/asset_discovery_manager.h"

#include <utility>

#include "base/strings/strcat.h"
#include "brave/components/brave_wallet/browser/blockchain_registry.h"
#include "brave/components/brave_wallet/browser/brave_wallet_constants.h"
#include "brave/components/brave_wallet/browser/brave_wallet_service.h"
#include "brave/components/brave_wallet/browser/brave_wallet_utils.h"
#include "brave/components/brave_wallet/browser/json_rpc_service.h"
#include "brave/components/brave_wallet/browser/pref_names.h"
#include "brave/components/brave_wallet/common/eth_address.h"
#include "brave/components/brave_wallet/common/hex_utils.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "ui/base/l10n/l10n_util.h"

namespace brave_wallet {

AssetDiscoveryManager::AssetDiscoveryManager(BraveWalletService* wallet_service,
                                             JsonRpcService* json_rpc_service,
                                             PrefService* prefs)
    : wallet_service_(wallet_service),
      json_rpc_service_(json_rpc_service),
      prefs_(prefs),
      weak_ptr_factory_(this) {}

AssetDiscoveryManager::~AssetDiscoveryManager() = default;

void AssetDiscoveryManager::DiscoverAssets(
    const std::string& chain_id,
    mojom::CoinType coin,
    const std::vector<std::string>& account_addresses,
    bool update_next_asset_discovery_from_block,
    const std::string& from_block,
    const std::string& to_block) {
  // Asset discovery only supported on select EVM chains
  if (coin != mojom::CoinType::ETH ||
      !base::Contains(GetAssetDiscoverySupportedChains(), chain_id)) {
    CompleteDiscoverAssets(
        chain_id, std::vector<mojom::BlockchainTokenPtr>(),
        mojom::ProviderError::kMethodNotSupported,
        l10n_util::GetStringUTF8(IDS_WALLET_METHOD_NOT_SUPPORTED_ERROR));
    return;
  }

  // Asset discovery only supported when using Infura proxy
  GURL infura_url = GetInfuraURLForKnownChainId(chain_id);
  GURL active_url = GetNetworkURL(prefs_, chain_id, coin);
  if (infura_url.host() != active_url.host()) {
    CompleteDiscoverAssets(
        chain_id, std::vector<mojom::BlockchainTokenPtr>(),
        mojom::ProviderError::kMethodNotSupported,
        l10n_util::GetStringUTF8(IDS_WALLET_METHOD_NOT_SUPPORTED_ERROR));
    return;
  }

  if (account_addresses.empty()) {
    CompleteDiscoverAssets(
        chain_id, std::vector<mojom::BlockchainTokenPtr>(),
        mojom::ProviderError::kInvalidParams,
        l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS));
    return;
  }

  for (const auto& account_address : account_addresses) {
    if (!EthAddress::IsValidAddress(account_address)) {
      CompleteDiscoverAssets(
          chain_id, std::vector<mojom::BlockchainTokenPtr>(),
          mojom::ProviderError::kInvalidParams,
          l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS));
      return;
    }
  }

  std::vector<mojom::BlockchainTokenPtr> user_assets =
      BraveWalletService::GetUserAssets(chain_id, mojom::CoinType::ETH, prefs_);
  auto internal_callback = base::BindOnce(
      &AssetDiscoveryManager::OnGetAllTokensDiscoverAssets,
      weak_ptr_factory_.GetWeakPtr(), chain_id, account_addresses,
      std::move(user_assets), update_next_asset_discovery_from_block,
      from_block, to_block);

  BlockchainRegistry::GetInstance()->GetAllTokens(
      chain_id, mojom::CoinType::ETH, std::move(internal_callback));
}

void AssetDiscoveryManager::OnGetAllTokensDiscoverAssets(
    const std::string& chain_id,
    const std::vector<std::string>& account_addresses,
    std::vector<mojom::BlockchainTokenPtr> user_assets,
    bool update_next_asset_discovery_from_block,
    const std::string& from_block,
    const std::string& to_block,
    std::vector<mojom::BlockchainTokenPtr> token_registry) {
  auto network_url = GetNetworkURL(prefs_, chain_id, mojom::CoinType::ETH);
  if (!network_url.is_valid()) {
    CompleteDiscoverAssets(
        chain_id, std::vector<mojom::BlockchainTokenPtr>(),
        mojom::ProviderError::kInvalidParams,
        l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS));
    return;
  }

  base::Value::List topics;
  if (!MakeAssetDiscoveryTopics(account_addresses, &topics)) {
    CompleteDiscoverAssets(
        chain_id, std::vector<mojom::BlockchainTokenPtr>(),
        mojom::ProviderError::kInvalidParams,
        l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS));
    return;
  }

  // Create set of contract addresses the user already has for easy lookups
  base::flat_set<std::string> user_asset_contract_addresses;
  for (const auto& user_asset : user_assets) {
    user_asset_contract_addresses.insert(user_asset->contract_address);
  }

  // Create a list of contract addresses to search by removing
  // all erc20s and assets the user has already added.
  base::Value::List contract_addresses_to_search;
  // Also create a map for addresses to blockchain tokens for easy lookup
  // for blockchain tokens in OnGetTransferLogs
  base::flat_map<std::string, mojom::BlockchainTokenPtr> tokens_to_search;
  for (auto& registry_token : token_registry) {
    if (registry_token->is_erc20 && !registry_token->contract_address.empty() &&
        !user_asset_contract_addresses.contains(
            registry_token->contract_address)) {
      // Use lowercase representation of hex address for comparisons
      // because providers may return all lowercase addresses.
      const std::string lower_case_contract_address =
          base::ToLowerASCII(registry_token->contract_address);
      contract_addresses_to_search.Append(lower_case_contract_address);
      tokens_to_search[lower_case_contract_address] = std::move(registry_token);
    }
  }

  if (contract_addresses_to_search.size() == 0) {
    AssetDiscoveryManager::CompleteDiscoverAssets(
        chain_id, std::vector<mojom::BlockchainTokenPtr>(),
        mojom::ProviderError::kSuccess, "");
    return;
  }

  auto callback = base::BindOnce(
      &AssetDiscoveryManager::OnGetTransferLogs, weak_ptr_factory_.GetWeakPtr(),
      base::OwnedRef(std::move(tokens_to_search)),
      update_next_asset_discovery_from_block, chain_id);

  json_rpc_service_->EthGetLogs(chain_id, from_block, to_block,
                                std::move(contract_addresses_to_search),
                                std::move(topics), std::move(callback));
}

void AssetDiscoveryManager::OnGetTransferLogs(
    base::flat_map<std::string, mojom::BlockchainTokenPtr>& tokens_to_search,
    bool update_next_asset_discovery_from_block,
    const std::string& chain_id,
    const std::vector<Log>& logs,
    mojom::ProviderError error,
    const std::string& error_message) {
  if (error != mojom::ProviderError::kSuccess) {
    CompleteDiscoverAssets(chain_id, std::vector<mojom::BlockchainTokenPtr>(),
                           std::move(error), error_message);
    return;
  }

  // Create unique list of addresses that matched eth_getLogs query
  // and keep track of largest block discovered
  base::flat_set<std::string> matching_contract_addresses;
  uint256_t largest_block = 0;
  for (const auto& log : logs) {
    matching_contract_addresses.insert(base::ToLowerASCII(log.address));
    if (log.block_number > largest_block) {
      largest_block = log.block_number;
    }
  }
  std::vector<mojom::BlockchainTokenPtr> discovered_assets;

  for (const auto& contract_address : matching_contract_addresses) {
    if (!tokens_to_search.contains(contract_address)) {
      continue;
    }
    mojom::BlockchainTokenPtr token =
        std::move(tokens_to_search.at(contract_address));

    if (!BraveWalletService::AddUserAsset(token.Clone(), prefs_)) {
      continue;
    }
    discovered_assets.push_back(std::move(token));
  }
  if (update_next_asset_discovery_from_block) {
    DictionaryPrefUpdate update(prefs_,
                                kBraveWalletNextAssetDiscoveryFromBlocks);
    auto* next_asset_discovery_from_blocks = update.Get()->GetIfDict();
    DCHECK(next_asset_discovery_from_blocks);
    const auto path = base::StrCat({kEthereumPrefKey, ".", chain_id});
    const std::string* current =
        next_asset_discovery_from_blocks->FindStringByDottedPath(path);
    uint256_t current_int = 0;
    if (current) {
      if (!HexValueToUint256(*current, &current_int)) {
        CompleteDiscoverAssets(
            chain_id, std::vector<mojom::BlockchainTokenPtr>(),
            mojom::ProviderError::kInternalError,
            l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR));
        return;
      }
    }
    if ((!current || current_int <= largest_block) && largest_block > 0) {
      next_asset_discovery_from_blocks->SetByDottedPath(
          path, Uint256ValueToHex(largest_block + 1));
    }
  }
  CompleteDiscoverAssets(chain_id, std::move(discovered_assets),
                         mojom::ProviderError::kSuccess, "");
}

void AssetDiscoveryManager::CompleteDiscoverAssets(
    const std::string& chain_id,
    std::vector<mojom::BlockchainTokenPtr> discovered_assets,
    mojom::ProviderError error,
    const std::string& error_message) {
  wallet_service_->OnDiscoveredAssetsCompleted(
      chain_id, std::move(error), error_message, std::move(discovered_assets));
}

void AssetDiscoveryManager::DiscoverAssetsOnAllSupportedChains(
    const std::vector<std::string>& account_addresses) {
  for (const auto& chain_id : GetAssetDiscoverySupportedChains()) {
    DiscoverAssets(chain_id, mojom::CoinType::ETH, account_addresses, false,
                   kEthereumBlockTagEarliest, kEthereumBlockTagLatest);
  }
}

void AssetDiscoveryManager::DiscoverAssetsOnAllSupportedChainsOnRefresh(
    const std::vector<std::string>& account_addresses) {
  // Simple client side rate limiting (only applies to refreshes)
  const base::Time assets_last_discovered_at =
      prefs_->GetTime(kBraveWalletLastDiscoveredAssetsAt);
  if (!assets_last_discovered_at.is_null() &&
      ((base::Time::Now() - base::Minutes(kAssetDiscoveryMinutesPerRequest)) <
       assets_last_discovered_at)) {
    return;
  }
  prefs_->SetTime(kBraveWalletLastDiscoveredAssetsAt, base::Time::Now());

  // Fetch block numbers for which asset discovery has been run through
  auto& next_asset_discovery_from_blocks =
      prefs_->GetDict(kBraveWalletNextAssetDiscoveryFromBlocks);
  for (const auto& chain_id : GetAssetDiscoverySupportedChains()) {
    // Call DiscoverAssets for the supported chain ID
    // using the kBraveWalletNextAssetDiscoveryFromBlocks pref
    // as the from_block of the eth_getLogs query.
    std::string from_block = kEthereumBlockTagEarliest;
    std::string to_block = kEthereumBlockTagLatest;
    const auto path = base::StrCat({kEthereumPrefKey, ".", chain_id});
    const std::string* next_asset_discovery_from_block =
        next_asset_discovery_from_blocks.FindStringByDottedPath(path);
    if (next_asset_discovery_from_block) {
      from_block = *next_asset_discovery_from_block;
    }

    DiscoverAssets(chain_id, mojom::CoinType::ETH, account_addresses, true,
                   from_block, to_block);
  }
}

void AssetDiscoveryManager::AccountsAdded(
    mojom::CoinType coin,
    const std::vector<std::string>& addresses) {
  if (coin != mojom::CoinType::ETH || addresses.size() == 0u) {
    return;
  }
  AssetDiscoveryManager::DiscoverAssetsOnAllSupportedChains(
      std::move(addresses));
}

}  // namespace brave_wallet
