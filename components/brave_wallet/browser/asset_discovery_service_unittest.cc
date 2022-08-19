/* Copyright (c) 2021 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "brave/components/brave_wallet/browser/asset_discovery_service.h"

#include <memory>
#include <utility>

#include "base/test/bind.h"
#include "brave/browser/brave_wallet/brave_wallet_service_factory.h"
#include "brave/browser/brave_wallet/json_rpc_service_factory.h"
#include "brave/browser/brave_wallet/keyring_service_factory.h"
#include "brave/browser/brave_wallet/tx_service_factory.h"
#include "brave/components/brave_wallet/browser/blockchain_list_parser.h"
#include "brave/components/brave_wallet/browser/blockchain_registry.h"
#include "brave/components/brave_wallet/browser/brave_wallet_constants.h"
#include "brave/components/brave_wallet/browser/brave_wallet_service.h"
#include "brave/components/brave_wallet/browser/json_rpc_service.h"
#include "brave/components/brave_wallet/browser/keyring_service.h"
#include "brave/components/brave_wallet/browser/tx_service.h"
#include "chrome/browser/prefs/browser_prefs.h"
#include "chrome/test/base/testing_profile.h"
#include "components/grit/brave_components_strings.h"
#include "components/prefs/pref_service.h"
#include "components/sync_preferences/testing_pref_service_syncable.h"
#include "content/public/test/browser_task_environment.h"
#include "services/data_decoder/public/cpp/test_support/in_process_data_decoder.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "services/network/test/test_url_loader_factory.h"
#include "testing/gmock/include/gmock/gmock.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace {

const char kMnemonic[] =
    "divide cruise upon flag harsh carbon filter merit once advice bright "
    "drive";
const char kPassword[] = "password";

}  // namespace

namespace brave_wallet {

class AssetDiscoveryServiceUnitTest : public testing::Test {
 public:
  AssetDiscoveryServiceUnitTest()
      : task_environment_(base::test::TaskEnvironment::TimeSource::MOCK_TIME) {}
  ~AssetDiscoveryServiceUnitTest() override = default;

  void SetInterceptor(const std::string& content) {
    url_loader_factory_.SetInterceptor(base::BindLambdaForTesting(
        [&, content](const network::ResourceRequest& request) {
          url_loader_factory_.ClearResponses();
          url_loader_factory_.AddResponse(request.url.spec(), content);
        }));
  }

  void SetHTTPRequestTimeoutInterceptor() {
    url_loader_factory_.SetInterceptor(base::BindLambdaForTesting(
        [&](const network::ResourceRequest& request) {
          url_loader_factory_.ClearResponses();
          url_loader_factory_.AddResponse(request.url.spec(), "",
                                          net::HTTP_REQUEST_TIMEOUT);
        }));
  }

 protected:
  void SetUp() override {
    TestingProfile::Builder builder;
    auto prefs =
        std::make_unique<sync_preferences::TestingPrefServiceSyncable>();
    RegisterUserProfilePrefs(prefs->registry());
    builder.SetPrefService(std::move(prefs));
    profile_ = builder.Build();
    shared_url_loader_factory_ =
        base::MakeRefCounted<network::WeakWrapperSharedURLLoaderFactory>(
            &url_loader_factory_);
    json_rpc_service_ =
        JsonRpcServiceFactory::GetServiceForContext(browser_context());
    json_rpc_service_->SetAPIRequestHelperForTesting(
        shared_url_loader_factory_);
    keyring_service_ =
        KeyringServiceFactory::GetServiceForContext(browser_context());
    tx_service_ = TxServiceFactory::GetServiceForContext(browser_context());
    wallet_service_ =
        BraveWalletServiceFactory::GetServiceForContext(browser_context());
    asset_discovery_service_.reset(new brave_wallet::AssetDiscoveryService(
        wallet_service_.get(), keyring_service_.get(),
        json_rpc_service_.get()));
  }

  PrefService* GetPrefs() { return profile_->GetPrefs(); }

  content::BrowserContext* browser_context() { return profile_.get(); }

  content::BrowserTaskEnvironment task_environment_;

  static bool RestoreWallet(const std::string& mnemonic,
                            KeyringService* service) {
    bool success = false;
    base::RunLoop run_loop;
    service->RestoreWallet(mnemonic, kPassword, false,
                           base::BindLambdaForTesting([&](bool v) {
                             success = v;
                             run_loop.Quit();
                           }));
    run_loop.Run();
    return success;
  }

  void GetUserAssets(const std::string& chain_id,
                     mojom::CoinType coin_type,
                     std::vector<mojom::BlockchainTokenPtr>* out_tokens) {
    base::RunLoop run_loop;
    wallet_service_->GetUserAssets(
        chain_id, coin_type,
        base::BindLambdaForTesting(
            [&](std::vector<mojom::BlockchainTokenPtr> tokens) {
              *out_tokens = std::move(tokens);
              run_loop.Quit();
            }));
    run_loop.Run();
  }

  void GetAllTokens(const std::string& chain_id,
                    mojom::CoinType coin_type,
                    std::vector<mojom::BlockchainTokenPtr>* out_tokens) {
    base::RunLoop run_loop;
    auto* blockchain_registry = BlockchainRegistry::GetInstance();
    blockchain_registry->GetAllTokens(
        chain_id, coin_type,
        base::BindLambdaForTesting(
            [&](std::vector<mojom::BlockchainTokenPtr> tokens) {
              *out_tokens = std::move(tokens);
              run_loop.Quit();
            }));
    run_loop.Run();
  }

  std::unique_ptr<AssetDiscoveryService> asset_discovery_service_;
  raw_ptr<BraveWalletService> wallet_service_;
  raw_ptr<JsonRpcService> json_rpc_service_;
  raw_ptr<KeyringService> keyring_service_;
  raw_ptr<TxService> tx_service_;

 private:
  std::unique_ptr<TestingProfile> profile_;
  network::TestURLLoaderFactory url_loader_factory_;
  scoped_refptr<network::SharedURLLoaderFactory> shared_url_loader_factory_;
  data_decoder::test::InProcessDataDecoder in_process_data_decoder_;
};

TEST_F(AssetDiscoveryServiceUnitTest, AccountsAdded) {
  // Add disoverable and non-discoverable tokens to registry:
  //   * Discoverable: Mainnet Dai, Mainnet USDC, Mainnet USDC
  //   * Non-discoverable: Mainnet LilNouns (an ERC721), and Avanche JOE
  auto* blockchain_registry = BlockchainRegistry::GetInstance();
  TokenListMap token_list_map;
  ASSERT_TRUE(ParseTokenList(R"( {
      "0x6b175474e89094c44da98b954eedeac495271d0f": {
        "name": "Dai Stablecoin",
        "logo": "dai.svg",
        "erc20": true,
        "symbol": "DAI",
        "decimals": 18,
        "chainId": "0x1"
      },
      "0x4b10701Bfd7BFEdc47d50562b76b436fbB5BdB3B": {
        "name": "Lil Nouns",
        "logo": "lilnouns.svg",
        "erc20": false,
        "erc721": true,
        "symbol": "LilNouns",
        "chainId": "0x1"
      },
      "0x6e84a6216eA6dACC71eE8E6b0a5B7322EEbC0fDd": {
        "name": "JoeToken",
        "logo": "joe.svg",
        "erc20": true,
        "symbol": "JOE",
        "decimals": 18,
        "chainId": "0xa86a"
      },
      "0xA0b86991c6218b36c1d19D4a2e9Eb0cE3606eB48": {
        "name": "USD Coin",
        "logo": "usdc.svg",
        "erc20": true,
        "symbol": "USDC",
        "decimals": 18,
        "chainId": "0x1"
      },
      "0xC02aaA39b223FE8D0A0e5C4F27eAD9083C756Cc2": {
        "name": "Wrapped Eth",
        "logo": "weth.svg",
        "erc20": true,
        "symbol": "WETH",
        "decimals": 18,
        "chainId": "0x1"
      }
    })",
                             &token_list_map, mojom::CoinType::ETH));
  blockchain_registry->UpdateTokenList(std::move(token_list_map));

  // Add DAI, LilNoun, and Joe transfer events for user address,
  // 0xf81229FE54D8a20fBc1e1e2a3451D1c7489437Db to ethLogs response
  // so it is discovered
  std::string eth_get_logs_response = R"(
   {"jsonrpc": "2.0",
    "id": 1,
    "result": [
      {
        "address": "0x6b175474e89094c44da98b954eedeac495271d0f",
        "blockHash": "0x2961ceb6c16bab72a55f79e394a35f2bf1c62b30446e3537280f7c22c3115e6e",
        "blockNumber": "0xd6464c",
        "data": "0x00000000000000000000000000000000000000000000000555aff1f0fae8c000",
        "logIndex": "0x159",
        "removed": false,
        "topics": [
          "0xddf252ad1be2c89b69c2b068fc378daa952ba7f163c4a11628f55a4df523b3ef",
          "0x000000000000000000000000503828976d22510aad0201ac7ec88293211d23da",
          "0x000000000000000000000000f81229FE54D8a20fBc1e1e2a3451D1c7489437Db"
        ],
        "transactionHash": "0x2e652b70966c6a05f4b3e68f20d6540b7a5ab712385464a7ccf62774d39b7066",
        "transactionIndex": "0x9f"
      },
      {
        "address": "0x4b10701Bfd7BFEdc47d50562b76b436fbB5BdB3B",
        "blockHash": "0x2961ceb6c16bab72a55f79e394a35f2bf1c62b30446e3537280f7c22c3115e6e",
        "blockNumber": "0xd6464c",
        "data": "0x00000000000000000000000000000000000000000000000555aff1f0fae8c000",
        "logIndex": "0x159",
        "removed": false,
        "topics": [
          "0xddf252ad1be2c89b69c2b068fc378daa952ba7f163c4a11628f55a4df523b3ef",
          "0x000000000000000000000000503828976d22510aad0201ac7ec88293211d23da",
          "0x000000000000000000000000f81229FE54D8a20fBc1e1e2a3451D1c7489437Db"
        ],
        "transactionHash": "0x2e652b70966c6a05f4b3e68f20d6540b7a5ab712385464a7ccf62774d39b7066",
        "transactionIndex": "0x9f"
      },
      {
        "address": "0x6e84a6216eA6dACC71eE8E6b0a5B7322EEbC0fDd",
        "blockHash": "0x2961ceb6c16bab72a55f79e394a35f2bf1c62b30446e3537280f7c22c3115e6e",
        "blockNumber": "0xd6464c",
        "data": "0x00000000000000000000000000000000000000000000000555aff1f0fae8c000",
        "logIndex": "0x159",
        "removed": false,
        "topics": [
          "0xddf252ad1be2c89b69c2b068fc378daa952ba7f163c4a11628f55a4df523b3ef",
          "0x000000000000000000000000503828976d22510aad0201ac7ec88293211d23da",
          "0x000000000000000000000000f81229FE54D8a20fBc1e1e2a3451D1c7489437Db"
        ],
        "transactionHash": "0x2e652b70966c6a05f4b3e68f20d6540b7a5ab712385464a7ccf62774d39b7066",
        "transactionIndex": "0x9f"
      },
    ]
   })";
  SetInterceptor(eth_get_logs_response);

  // RestoreWallet adds account 0xf81229FE54D8a20fBc1e1e2a3451D1c7489437Db and
  // triggers asset discovery
  ASSERT_TRUE(RestoreWallet(kMnemonic, keyring_service_));
  base::RunLoop().RunUntilIdle();

  // DAI should have been added to user assets, LilNoun and JOE should not have
  std::vector<mojom::BlockchainTokenPtr> user_assets;
  GetUserAssets(mojom::kMainnetChainId, mojom::CoinType::ETH, &user_assets);
  EXPECT_EQ(user_assets.size(), 3u);  // BAT and ETH (preloaded) + DAI = 3u
  EXPECT_EQ(user_assets[2]->name, "Dai Stablecoin");

  // Add WETH and USDC Transfer events to eth_getLogs response
  // for next accounts to be restored
  // 0x00c0f72E601C31DEb7890612cB92Ac0Fb7090EB0, and
  // 0xc103691F9E860104E5A0605449005Ebf51450258 respectively
  eth_get_logs_response = R"(
   {"jsonrpc": "2.0",
    "id": 1,
    "result": [
      {
        "address": "0xC02aaA39b223FE8D0A0e5C4F27eAD9083C756Cc2",
        "blockHash": "0x2961ceb6c16bab72a55f79e394a35f2bf1c62b30446e3537280f7c22c3115e6e",
        "blockNumber": "0xd6464c",
        "data": "0x00000000000000000000000000000000000000000000000555aff1f0fae8c000",
        "logIndex": "0x159",
        "removed": false,
        "topics": [
          "0xddf252ad1be2c89b69c2b068fc378daa952ba7f163c4a11628f55a4df523b3ef",
          "0x000000000000000000000000503828976d22510aad0201ac7ec88293211d23da",
          "0x00000000000000000000000000c0f72E601C31DEb7890612cB92Ac0Fb7090EB0",
        ],
        "transactionHash": "0x2e652b70966c6a05f4b3e68f20d6540b7a5ab712385464a7ccf62774d39b7066",
        "transactionIndex": "0x9f"
      },
      {
        "address": "0xA0b86991c6218b36c1d19D4a2e9Eb0cE3606eB48",
        "blockHash": "0x2961ceb6c16bab72a55f79e394a35f2bf1c62b30446e3537280f7c22c3115e6e",
        "blockNumber": "0xd6464c",
        "data": "0x00000000000000000000000000000000000000000000000555aff1f0fae8c000",
        "logIndex": "0x159",
        "removed": false,
        "topics": [
          "0xddf252ad1be2c89b69c2b068fc378daa952ba7f163c4a11628f55a4df523b3ef",
          "0x000000000000000000000000503828976d22510aad0201ac7ec88293211d23da",
          "0x000000000000000000000000c103691F9E860104E5A0605449005Ebf51450258"
        ],
        "transactionHash": "0x2e652b70966c6a05f4b3e68f20d6540b7a5ab712385464a7ccf62774d39b7066",
        "transactionIndex": "0x9f"
      }
    ]
   })";
  SetInterceptor(eth_get_logs_response);
  keyring_service_->AddAccountsWithDefaultName(2);
  base::RunLoop().RunUntilIdle();
  GetUserAssets(mojom::kMainnetChainId, mojom::CoinType::ETH, &user_assets);
  EXPECT_EQ(user_assets.size(), 5u);  // BAT and ETH (preloaded) + DAI = 3u
  EXPECT_EQ(user_assets[3]->name, "USD Coin");
  EXPECT_EQ(user_assets[4]->name, "Wrapped Eth");

  // HTTP Timeout should be handled gracefully
  SetHTTPRequestTimeoutInterceptor();
  keyring_service_->AddAccountsWithDefaultName(1);
  base::RunLoop().RunUntilIdle();
  GetUserAssets(mojom::kMainnetChainId, mojom::CoinType::ETH, &user_assets);
  EXPECT_EQ(user_assets.size(), 5u);

  // TODO(nvonpentz) Adding Solana accounts should not trigger asset discovery
  // service.AddAccountForKeyring(mojom::kSolanaKeyringId, "Account 99");
  // base::RunLoop().RunUntilIdle();
  // GetUserAssets(mojom::kMainnetChainId, mojom::CoinType::ETH, &user_assets);
  // EXPECT_EQ(user_assets.size(), 5u);
  // How to assert no request made / JsonRpcService::DiscoverAssets not called?

  // TODO(nvonpentz) If asset is already discovered, no request should be made
  // How to assert no request made / JsonRpcService::DiscoverAssets not called?
}

}  // namespace brave_wallet
