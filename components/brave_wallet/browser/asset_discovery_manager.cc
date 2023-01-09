/* Copyright 2022 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "brave/components/brave_wallet/browser/asset_discovery_manager.h"

#include <utility>

#include "base/task/task_runner.h"
#include "base/task/task_runner_util.h"
#include "base/task/thread_pool.h"

#include "base/base64.h"
#include "base/strings/strcat.h"
#include "brave/components/brave_wallet/browser/blockchain_registry.h"
#include "brave/components/brave_wallet/browser/brave_wallet_service.h"
#include "brave/components/brave_wallet/browser/brave_wallet_utils.h"
#include "brave/components/brave_wallet/browser/eth_topics_builder.h"
#include "brave/components/brave_wallet/browser/json_rpc_service.h"
#include "brave/components/brave_wallet/browser/keyring_service.h"
#include "brave/components/brave_wallet/browser/pref_names.h"
#include "brave/components/brave_wallet/common/eth_address.h"
#include "brave/components/brave_wallet/common/hex_utils.h"
#include "brave/components/brave_wallet/common/solana_utils.h"
#include "components/prefs/pref_service.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "ui/base/l10n/l10n_util.h"

#include "base/threading/platform_thread.h"
#include "base/time/time.h"

namespace brave_wallet {

AssetDiscoveryManager::AssetDiscoveryManager(BraveWalletService* wallet_service,
                                             JsonRpcService* json_rpc_service,
                                             KeyringService* keyring_service,
                                             PrefService* prefs)
    : wallet_service_(wallet_service),
      json_rpc_service_(json_rpc_service),
      keyring_service_(keyring_service),
      prefs_(prefs),
      sequenced_task_runner_(
          base::ThreadPool::CreateSequencedTaskRunner({base::MayBlock()})),
      weak_ptr_factory_(this) {
  keyring_service_->AddObserver(
      keyring_service_observer_receiver_.BindNewPipeAndPassRemote());
}

AssetDiscoveryManager::~AssetDiscoveryManager() = default;

const std::vector<std::string>&
AssetDiscoveryManager::GetAssetDiscoverySupportedChains() {
  if (supported_chains_for_testing_.size() > 0) {
    return supported_chains_for_testing_;
  }
  static base::NoDestructor<std::vector<std::string>>
      asset_discovery_supported_chains({mojom::kMainnetChainId});
  return *asset_discovery_supported_chains;
}

void AssetDiscoveryManager::DiscoverSolanaAssets(
    const std::vector<std::string>& account_addresses,
    bool triggered_by_accounts_added) {
  if (account_addresses.empty()) {
    CompleteDiscoverAssets(
        mojom::kSolanaMainnet, std::vector<mojom::BlockchainTokenPtr>(),
        mojom::ProviderErrorUnion::NewSolanaProviderError(
            mojom::SolanaProviderError::kInvalidParams),
        l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS),
        triggered_by_accounts_added);
    return;
  }

  // TODO(nvonpentz): When custom networks are supported, we need to check
  // that the active network is our own that supports this RPC call.

  const auto barrier_callback = base::BarrierCallback<std::vector<std::string>>(
      account_addresses.size(),
      base::BindOnce(&AssetDiscoveryManager::MergeDiscoveredSolanaAssets,
                     weak_ptr_factory_.GetWeakPtr()));
  VLOG(0) << "account addresses.size(): " << account_addresses.size();
  for (const auto& account_address : account_addresses) {
    json_rpc_service_->GetSolanaTokenAccountsByOwner(
        account_address,
        base::BindOnce(&AssetDiscoveryManager::OnGetSolanaTokenAccountsByOwner,
                       weak_ptr_factory_.GetWeakPtr(),
                       std::move(barrier_callback)));
  }
}

void AssetDiscoveryManager::OnGetSolanaTokenAccountsByOwner(
    base::OnceCallback<void(std::vector<std::string>)> barrier_callback,
    const std::vector<absl::optional<SolanaAccountInfo>>& token_accounts,
    mojom::SolanaProviderError error,
    const std::string& error_message) {
  if (error != mojom::SolanaProviderError::kSuccess || token_accounts.empty()) {
    std::move(barrier_callback).Run(std::vector<std::string>());
    return;
  }

  VLOG(0) << "AssetDiscoveryManager::OnGetSolanaTokenAccountsByOwner 0, "
             "token_accounts size "
          << token_accounts.size();
  // Add each token account to the all_discovered_contract_addresses list
  std::vector<std::string> discovered_contract_addresses;
  for (const auto& token_account : token_accounts) {
    // Get the contract address
    if (token_account.has_value()) {
      // Decode Base64
      const absl::optional<std::vector<uint8_t>> data =
          base::Base64Decode(token_account->data);
      if (data.has_value()) {
        // Decode the address
        const absl::optional<std::string> contract_address =
            DecodeContractAddress(data.value());
        if (contract_address.has_value()) {
          // Add the contract address to the list
          discovered_contract_addresses.push_back(contract_address.value());
        }
      }
    }
  }

  VLOG(0) << "AssetDiscoveryManager::OnGetSolanaTokenAccountsByOwner 1, "
             "discovered_contract_addresses size "
          << discovered_contract_addresses.size();
  std::move(barrier_callback).Run(std::move(discovered_contract_addresses));
}

void AssetDiscoveryManager::MergeDiscoveredSolanaAssets(
    const std::vector<std::vector<std::string>>&
        all_discovered_contract_addresses) {
  VLOG(0) << "AssetDiscoveryManager::MergeDiscoveredSolanaAssets";
  // Create unique flat_set of all discovered contract addresses
  base::flat_set<std::string> discovered_contract_addresses;
  for (const auto& discovered_contract_address_list :
       all_discovered_contract_addresses) {
    for (const auto& discovered_contract_address :
         discovered_contract_address_list) {
      VLOG(0) << "AssetDiscoveryManager::MergeDiscoveredSolanaAssets, "
                 "discovered_contract_address "
              << discovered_contract_address;
      discovered_contract_addresses.insert(discovered_contract_address);
    }
  }

  auto internal_callback = base::BindOnce(
      &AssetDiscoveryManager::OnGetSolanaTokenRegistry,
      weak_ptr_factory_.GetWeakPtr(), std::move(discovered_contract_addresses));

  // Fetch registry tokens
  BlockchainRegistry::GetInstance()->GetAllTokens(mojom::kSolanaMainnet,
                                                  mojom::CoinType::SOL,
                                                  std::move(internal_callback));
}

void AssetDiscoveryManager::OnGetSolanaTokenRegistry(
    const base::flat_set<std::string>& discovered_contract_addresses,
    std::vector<mojom::BlockchainTokenPtr> sol_token_registry) {
  VLOG(0) << "AssetDiscoveryManager::OnGetSolanaTokenRegistry 0";
  std::vector<mojom::BlockchainTokenPtr> discovered_tokens;
  for (const auto& token : sol_token_registry) {
    if (discovered_contract_addresses.contains(token->contract_address)) {
      VLOG(0) << "AssetDiscoveryManager::OnGetSolanaTokenRegistry, "
                 "token->contract_address "
              << token->contract_address;
      if (!BraveWalletService::AddUserAsset(token.Clone(), prefs_)) {
        VLOG(0) << "AssetDiscoveryManager::OnGetSolanaTokenRegistry, "
                   "failed to add token "
                << token->contract_address;
        continue;
      }
      discovered_tokens.push_back(token.Clone());
      VLOG(0) << "AssetDiscoveryManager::OnGetSolanaTokenRegistry 0, token "
                 "contract address "
              << token->contract_address;
    }
  }

  VLOG(0) << "AssetDiscoveryManager::OnGetSolanaTokenRegistry 1, "
             "discovered_tokens.size() "
          << discovered_tokens.size();

  CompleteDiscoverAssets(mojom::kSolanaMainnet, std::move(discovered_tokens),
                         mojom::ProviderErrorUnion::NewSolanaProviderError(
                             mojom::SolanaProviderError::kSuccess),
                         "",
                         false);  // TODO
}

void AssetDiscoveryManager::DiscoverAssets(
    const std::string& chain_id,
    mojom::CoinType coin,
    const std::vector<std::string>& account_addresses,
    bool triggered_by_accounts_added,
    const std::string& from_block,
    const std::string& to_block) {
  if (account_addresses.empty()) {
    CompleteDiscoverAssets(
        chain_id, std::vector<mojom::BlockchainTokenPtr>(),
        mojom::ProviderErrorUnion::NewProviderError(
            mojom::ProviderError::kInvalidParams),
        l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS),
        triggered_by_accounts_added);
    return;
  }
  // TODO(nvonpentz): Probably should move most of this logic to an ETH specific
  // function and create another for Solana

  // Asset discovery only supported on select EVM chains
  if (coin != mojom::CoinType::ETH ||
      !base::Contains(GetAssetDiscoverySupportedChains(), chain_id)) {
    CompleteDiscoverAssets(
        chain_id, std::vector<mojom::BlockchainTokenPtr>(),
        mojom::ProviderErrorUnion::NewProviderError(
            mojom::ProviderError::kMethodNotSupported),
        l10n_util::GetStringUTF8(IDS_WALLET_METHOD_NOT_SUPPORTED_ERROR),
        triggered_by_accounts_added);
    return;
  }

  // Asset discovery only supported when using Infura proxy
  GURL infura_url = GetInfuraURLForKnownChainId(chain_id);
  GURL active_url = GetNetworkURL(prefs_, chain_id, coin);
  if (infura_url.host() != active_url.host()) {
    CompleteDiscoverAssets(
        chain_id, std::vector<mojom::BlockchainTokenPtr>(),
        mojom::ProviderErrorUnion::NewProviderError(
            mojom::ProviderError::kMethodNotSupported),
        l10n_util::GetStringUTF8(IDS_WALLET_METHOD_NOT_SUPPORTED_ERROR),
        triggered_by_accounts_added);
    return;
  }

  for (const auto& account_address : account_addresses) {
    if (!EthAddress::IsValidAddress(account_address)) {
      CompleteDiscoverAssets(
          chain_id, std::vector<mojom::BlockchainTokenPtr>(),
          mojom::ProviderErrorUnion::NewProviderError(
              mojom::ProviderError::kInvalidParams),
          l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS),
          triggered_by_accounts_added);
      return;
    }
  }

  std::vector<mojom::BlockchainTokenPtr> user_assets =
      BraveWalletService::GetUserAssets(chain_id, mojom::CoinType::ETH, prefs_);
  auto internal_callback =
      base::BindOnce(&AssetDiscoveryManager::OnGetAllTokensDiscoverAssets,
                     weak_ptr_factory_.GetWeakPtr(), chain_id,
                     account_addresses, std::move(user_assets),
                     triggered_by_accounts_added, from_block, to_block);

  BlockchainRegistry::GetInstance()->GetAllTokens(
      chain_id, mojom::CoinType::ETH, std::move(internal_callback));
}

void AssetDiscoveryManager::OnGetAllTokensDiscoverAssets(
    const std::string& chain_id,
    const std::vector<std::string>& account_addresses,
    std::vector<mojom::BlockchainTokenPtr> user_assets,
    bool triggered_by_accounts_added,
    const std::string& from_block,
    const std::string& to_block,
    std::vector<mojom::BlockchainTokenPtr> token_registry) {
  auto network_url = GetNetworkURL(prefs_, chain_id, mojom::CoinType::ETH);
  if (!network_url.is_valid()) {
    CompleteDiscoverAssets(
        chain_id, std::vector<mojom::BlockchainTokenPtr>(),
        mojom::ProviderErrorUnion::NewProviderError(
            mojom::ProviderError::kInvalidParams),
        l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS),
        triggered_by_accounts_added);
    return;
  }

  base::Value::List topics;
  if (!MakeAssetDiscoveryTopics(account_addresses, &topics)) {
    CompleteDiscoverAssets(
        chain_id, std::vector<mojom::BlockchainTokenPtr>(),
        mojom::ProviderErrorUnion::NewProviderError(
            mojom::ProviderError::kInvalidParams),
        l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS),
        triggered_by_accounts_added);
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
    CompleteDiscoverAssets(chain_id, std::vector<mojom::BlockchainTokenPtr>(),
                           mojom::ProviderErrorUnion::NewProviderError(
                               mojom::ProviderError::kSuccess),
                           "", triggered_by_accounts_added);
    return;
  }

  auto callback = base::BindOnce(&AssetDiscoveryManager::OnGetTransferLogs,
                                 weak_ptr_factory_.GetWeakPtr(),
                                 base::OwnedRef(std::move(tokens_to_search)),
                                 triggered_by_accounts_added, chain_id);

  json_rpc_service_->EthGetLogs(chain_id, from_block, to_block,
                                std::move(contract_addresses_to_search),
                                std::move(topics), std::move(callback));
}

void AssetDiscoveryManager::OnGetTransferLogs(
    base::flat_map<std::string, mojom::BlockchainTokenPtr>& tokens_to_search,
    bool triggered_by_accounts_added,
    const std::string& chain_id,
    const std::vector<Log>& logs,
    mojom::ProviderError error,
    const std::string& error_message) {
  if (error != mojom::ProviderError::kSuccess) {
    CompleteDiscoverAssets(chain_id, std::vector<mojom::BlockchainTokenPtr>(),
                           mojom::ProviderErrorUnion::NewProviderError(error),
                           error_message, triggered_by_accounts_added);
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

  if (!triggered_by_accounts_added) {
    // Update the last block discovered for this chain unless this
    // was triggered by accounts added
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
            mojom::ProviderErrorUnion::NewProviderError(
                mojom::ProviderError::kInternalError),
            l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR),
            triggered_by_accounts_added);
        return;
      }
    }
    if ((!current || current_int <= largest_block) && largest_block > 0) {
      next_asset_discovery_from_blocks->SetByDottedPath(
          path, Uint256ValueToHex(largest_block + 1));
    }
  }
  CompleteDiscoverAssets(chain_id, std::move(discovered_assets),
                         mojom::ProviderErrorUnion::NewProviderError(
                             mojom::ProviderError::kSuccess),
                         "", triggered_by_accounts_added);
}

void AssetDiscoveryManager::CompleteDiscoverAssets(
    const std::string& chain_id,
    std::vector<mojom::BlockchainTokenPtr> discovered_assets_for_chain,
    // TODO(nvonpentz): Since it's one discover assets call to many RPC requests
    // a single error does not make sense for Solana
    mojom::ProviderErrorUnionPtr error,
    const std::string& error_message,
    bool triggered_by_accounts_added) {
  if (discover_assets_completed_callback_for_testing_) {
    std::vector<mojom::BlockchainTokenPtr> discovered_assets_for_chain_clone;
    for (const auto& asset : discovered_assets_for_chain) {
      discovered_assets_for_chain_clone.push_back(asset.Clone());
    }
    discover_assets_completed_callback_for_testing_.Run(
        chain_id, std::move(discovered_assets_for_chain_clone),
        std::move(error), error_message);
  }

  // Do not emit event or modify remaining_chains_ count if DiscoverAssets call
  // was triggered by an AccountsAdded event
  if (triggered_by_accounts_added) {
    return;
  }

  // Complete the call by decrementing remaining_chains_, storing the discovered
  // assets for later, and emitting the event if this was the final chain to
  // finish
  remaining_chains_--;
  for (auto& asset : discovered_assets_for_chain) {
    discovered_assets_.push_back(std::move(asset));
  }

  if (remaining_chains_ == 0) {
    wallet_service_->OnDiscoverAssetsCompleted(std::move(discovered_assets_));
    discovered_assets_.clear();
  }
}

void AssetDiscoveryManager::DiscoverAssetsOnAllSupportedChainsAccountsAdded(
    mojom::CoinType coin,
    const std::vector<std::string>& account_addresses) {
  if (coin == mojom::CoinType::ETH) {
    for (const auto& chain_id : GetAssetDiscoverySupportedChains()) {
      DiscoverAssets(chain_id, mojom::CoinType::ETH, account_addresses, true,
                     kEthereumBlockTagEarliest, kEthereumBlockTagLatest);
    }
  } else {
    // TODO(nvonpentz) Add support for Solana
  }
}

void AssetDiscoveryManager::DiscoverAssetsOnAllSupportedChainsRefresh(
    const std::vector<std::string>& account_addresses) {
  // Simple client side rate limiting (only applies to refreshes)
  const base::Time assets_last_discovered_at =
      prefs_->GetTime(kBraveWalletLastDiscoveredAssetsAt);
  if (!assets_last_discovered_at.is_null() &&
      ((base::Time::Now() - base::Minutes(kAssetDiscoveryMinutesPerRequest)) <
       assets_last_discovered_at)) {
    wallet_service_->OnDiscoverAssetsCompleted({});
    return;
  }
  prefs_->SetTime(kBraveWalletLastDiscoveredAssetsAt, base::Time::Now());

  // Return early and do not send a notification
  // if a discover assets process is flight already
  if (remaining_chains_ != 0) {
    return;
  }

  // TODO(nvonpentz) Add support for Solana
  const std::vector<std::string>& supported_chain_ids =
      GetAssetDiscoverySupportedChains();
  remaining_chains_ = supported_chain_ids.size();

  // Fetch block numbers for which asset discovery has been run through
  auto& next_asset_discovery_from_blocks =
      prefs_->GetDict(kBraveWalletNextAssetDiscoveryFromBlocks);
  for (const auto& chain_id : supported_chain_ids) {
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

    DiscoverAssets(chain_id, mojom::CoinType::ETH, account_addresses, false,
                   from_block, to_block);
  }
}

void AssetDiscoveryManager::AccountsAdded(
    mojom::CoinType coin,
    const std::vector<std::string>& addresses) {
  if (!(coin == mojom::CoinType::ETH || coin == mojom::CoinType::SOL) ||
      addresses.size() == 0u) {
    return;
  }
  DiscoverAssetsOnAllSupportedChainsAccountsAdded(coin, addresses);
}

// static
absl::optional<std::string> AssetDiscoveryManager::DecodeContractAddress(
    const std::vector<uint8_t>& data) {
  // TODO Make this robust
  std::vector<uint8_t> pub_key_bytes(data.begin(), data.begin() + 32);
  std::string pub_key = Base58Encode(pub_key_bytes);
  return pub_key;
}

}  // namespace brave_wallet
