/* Copyright (c) 2021 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <memory>
#include <utility>
#include <vector>

#include "base/callback.h"
#include "base/json/json_reader.h"
#include "base/json/json_writer.h"
#include "base/strings/utf_string_conversions.h"
#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "base/values.h"
#include "brave/components/brave_wallet/browser/brave_wallet_constants.h"
#include "brave/components/brave_wallet/browser/brave_wallet_prefs.h"
#include "brave/components/brave_wallet/browser/brave_wallet_utils.h"
#include "brave/components/brave_wallet/browser/json_rpc_service.h"
#include "brave/components/brave_wallet/browser/keyring_service.h"
#include "brave/components/brave_wallet/browser/pref_names.h"
#include "brave/components/brave_wallet/common/brave_wallet.mojom.h"
#include "brave/components/brave_wallet/common/hash_utils.h"
#include "brave/components/brave_wallet/common/value_conversion_utils.h"
#include "brave/components/ipfs/ipfs_service.h"
#include "brave/components/ipfs/ipfs_utils.h"
#include "brave/components/ipfs/pref_names.h"
#include "components/prefs/scoped_user_pref_update.h"
#include "components/sync_preferences/testing_pref_service_syncable.h"
#include "content/public/browser/storage_partition.h"
#include "mojo/public/cpp/bindings/self_owned_receiver.h"
#include "net/test/embedded_test_server/embedded_test_server.h"
#include "net/test/embedded_test_server/http_request.h"
#include "net/test/embedded_test_server/http_response.h"
#include "services/data_decoder/public/cpp/test_support/in_process_data_decoder.h"
#include "services/network/public/cpp/weak_wrapper_shared_url_loader_factory.h"
#include "services/network/test/test_url_loader_factory.h"
#include "testing/gtest/include/gtest/gtest.h"
#include "ui/base/l10n/l10n_util.h"
#include "url/gurl.h"
#include "url/origin.h"

namespace brave_wallet {

namespace {

void GetErrorCodeMessage(base::Value formed_response,
                         mojom::ProviderError* error,
                         std::string* error_message) {
  if (!formed_response.is_dict()) {
    *error = mojom::ProviderError::kSuccess;
    error_message->clear();
    return;
  }
  const base::Value* code = formed_response.FindKey("code");
  if (code) {
    *error = static_cast<mojom::ProviderError>(code->GetInt());
  }
  const base::Value* message = formed_response.FindKey("message");
  if (message) {
    *error_message = message->GetString();
  }
}

void UpdateCustomNetworks(PrefService* prefs,
                          std::vector<base::Value>* values) {
  DictionaryPrefUpdate update(prefs, kBraveWalletCustomNetworks);
  base::Value* dict = update.Get();
  ASSERT_TRUE(dict);
  base::Value* list = dict->FindKey(kEthereumPrefKey);
  if (!list) {
    list = dict->SetKey(kEthereumPrefKey, base::Value(base::Value::Type::LIST));
  }
  ASSERT_TRUE(list);
  list->ClearList();
  for (auto& it : *values) {
    list->Append(std::move(it));
  }
}

void OnRequestResponse(bool* callback_called,
                       bool expected_success,
                       const std::string& expected_response,
                       base::Value id,
                       base::Value formed_response,
                       const bool reject,
                       const std::string& first_allowed_account,
                       const bool update_bind_js_properties) {
  *callback_called = true;
  std::string response;
  base::JSONWriter::Write(formed_response, &response);
  mojom::ProviderError error;
  std::string error_message;
  GetErrorCodeMessage(std::move(formed_response), &error, &error_message);
  bool success = error == brave_wallet::mojom::ProviderError::kSuccess;
  EXPECT_EQ(expected_success, success);
  if (!success) {
    response = "";
  }
  EXPECT_EQ(expected_response, response);
}

void OnStringResponse(bool* callback_called,
                      brave_wallet::mojom::ProviderError expected_error,
                      const std::string& expected_error_message,
                      const std::string& expected_response,
                      const std::string& response,
                      brave_wallet::mojom::ProviderError error,
                      const std::string& error_message) {
  *callback_called = true;
  EXPECT_EQ(expected_response, response);
  EXPECT_EQ(expected_error, error);
  EXPECT_EQ(expected_error_message, error_message);
}

void OnBoolResponse(bool* callback_called,
                    brave_wallet::mojom::ProviderError expected_error,
                    const std::string& expected_error_message,
                    bool expected_response,
                    bool response,
                    brave_wallet::mojom::ProviderError error,
                    const std::string& error_message) {
  *callback_called = true;
  EXPECT_EQ(expected_response, response);
  EXPECT_EQ(expected_error, error);
  EXPECT_EQ(expected_error_message, error_message);
}

void OnStringsResponse(bool* callback_called,
                       brave_wallet::mojom::ProviderError expected_error,
                       const std::string& expected_error_message,
                       const std::vector<std::string>& expected_response,
                       const std::vector<std::string>& response,
                       brave_wallet::mojom::ProviderError error,
                       const std::string& error_message) {
  *callback_called = true;
  EXPECT_EQ(expected_response, response);
  EXPECT_EQ(expected_error, error);
  EXPECT_EQ(expected_error_message, error_message);
}

class TestJsonRpcServiceObserver
    : public brave_wallet::mojom::JsonRpcServiceObserver {
 public:
  TestJsonRpcServiceObserver(base::OnceClosure callback,
                             const std::string& expected_chain_id,
                             mojom::CoinType expected_coin,
                             bool expected_error_empty) {
    callback_ = std::move(callback);
    expected_chain_id_ = expected_chain_id;
    expected_coin_ = expected_coin;
    expected_error_empty_ = expected_error_empty;
  }

  TestJsonRpcServiceObserver(const std::string& expected_chain_id,
                             mojom::CoinType expected_coin,
                             bool expected_is_eip1559) {
    expected_chain_id_ = expected_chain_id;
    expected_coin_ = expected_coin;
    expected_is_eip1559_ = expected_is_eip1559;
  }

  void Reset(const std::string& expected_chain_id, bool expected_is_eip1559) {
    expected_chain_id_ = expected_chain_id;
    expected_is_eip1559_ = expected_is_eip1559;
    chain_changed_called_ = false;
    is_eip1559_changed_called_ = false;
  }

  void OnAddEthereumChainRequestCompleted(const std::string& chain_id,
                                          const std::string& error) override {
    EXPECT_EQ(chain_id, expected_chain_id_);
    EXPECT_EQ(error.empty(), expected_error_empty_);
    std::move(callback_).Run();
  }

  void ChainChangedEvent(const std::string& chain_id,
                         mojom::CoinType coin) override {
    chain_changed_called_ = true;
    EXPECT_EQ(chain_id, expected_chain_id_);
    EXPECT_EQ(coin, expected_coin_);
  }

  void OnIsEip1559Changed(const std::string& chain_id,
                          bool is_eip1559) override {
    is_eip1559_changed_called_ = true;
    EXPECT_EQ(chain_id, expected_chain_id_);
    EXPECT_EQ(is_eip1559, expected_is_eip1559_);
  }

  bool is_eip1559_changed_called() {
    base::RunLoop().RunUntilIdle();
    return is_eip1559_changed_called_;
  }

  bool chain_changed_called() {
    base::RunLoop().RunUntilIdle();
    return chain_changed_called_;
  }

  ::mojo::PendingRemote<brave_wallet::mojom::JsonRpcServiceObserver>
  GetReceiver() {
    return observer_receiver_.BindNewPipeAndPassRemote();
  }

  base::OnceClosure callback_;
  std::string expected_chain_id_;
  mojom::CoinType expected_coin_;
  bool expected_error_empty_;
  bool expected_is_eip1559_;
  bool chain_changed_called_ = false;
  bool is_eip1559_changed_called_ = false;
  mojo::Receiver<brave_wallet::mojom::JsonRpcServiceObserver>
      observer_receiver_{this};
};

}  // namespace

class JsonRpcServiceUnitTest : public testing::Test {
 public:
  JsonRpcServiceUnitTest()
      : shared_url_loader_factory_(
            base::MakeRefCounted<network::WeakWrapperSharedURLLoaderFactory>(
                &url_loader_factory_)) {
    url_loader_factory_.SetInterceptor(base::BindLambdaForTesting(
        [&](const network::ResourceRequest& request) {
          url_loader_factory_.ClearResponses();
          url_loader_factory_.AddResponse(
              brave_wallet::GetNetworkURL(prefs(), mojom::kLocalhostChainId,
                                          mojom::CoinType::ETH)
                  .spec(),
              "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":"
              "\"0x000000000000000000000000000000000000000000000000000000000000"
              "0020000000000000000000000000000000000000000000000000000000000000"
              "0026e3010170122008ab7bf21b73828364305ef6b7c676c1f5a73e18ab4f93be"
              "ec7e21e0bc84010e000000000000000000000000000000000000000000000000"
              "0000\"}");
        }));

    brave_wallet::RegisterProfilePrefs(prefs_.registry());
    brave_wallet::RegisterProfilePrefsForMigration(prefs_.registry());
    ipfs::IpfsService::RegisterProfilePrefs(prefs_.registry());
    json_rpc_service_.reset(
        new JsonRpcService(shared_url_loader_factory_, &prefs_));
    SetNetwork(mojom::kLocalhostChainId, mojom::CoinType::ETH);
  }

  ~JsonRpcServiceUnitTest() override = default;

  scoped_refptr<network::SharedURLLoaderFactory> shared_url_loader_factory() {
    return shared_url_loader_factory_;
  }

  PrefService* prefs() { return &prefs_; }

  bool GetIsEip1559FromPrefs(const std::string& chain_id) {
    if (chain_id == mojom::kLocalhostChainId)
      return prefs()->GetBoolean(kSupportEip1559OnLocalhostChain);
    const base::Value* custom_networks =
        prefs()
            ->GetDictionary(kBraveWalletCustomNetworks)
            ->FindKey(kEthereumPrefKey);
    if (!custom_networks)
      return false;

    for (const auto& chain : custom_networks->GetList()) {
      if (!chain.is_dict())
        continue;

      const std::string* id = chain.FindStringKey("chainId");
      if (!id || *id != chain_id)
        continue;

      return chain.FindBoolKey("is_eip1559").value_or(false);
    }

    return false;
  }
  void SetEthChainIdInterceptor(const std::string& network_url,
                                const std::string& chain_id) {
    url_loader_factory_.SetInterceptor(base::BindLambdaForTesting(
        [&, network_url, chain_id](const network::ResourceRequest& request) {
          base::StringPiece request_string(request.request_body->elements()
                                               ->at(0)
                                               .As<network::DataElementBytes>()
                                               .AsStringPiece());
          url_loader_factory_.ClearResponses();
          if (request_string.find("eth_chainId") != std::string::npos) {
            url_loader_factory_.AddResponse(
                network_url, "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":\"" +
                                 chain_id + "\"}");
          }
        }));
  }
  void SetEthChainIdInterceptorWithBrokenResponse(
      const std::string& network_url) {
    url_loader_factory_.SetInterceptor(base::BindLambdaForTesting(
        [&, network_url](const network::ResourceRequest& request) {
          base::StringPiece request_string(request.request_body->elements()
                                               ->at(0)
                                               .As<network::DataElementBytes>()
                                               .AsStringPiece());
          url_loader_factory_.ClearResponses();
          if (request_string.find("eth_chainId") != std::string::npos) {
            url_loader_factory_.AddResponse(network_url, "{\"jsonrpc\":\"");
          }
        }));
  }

  void SetUDENSInterceptor(const std::string& chain_id) {
    GURL network_url =
        brave_wallet::GetNetworkURL(prefs(), chain_id, mojom::CoinType::ETH);
    ASSERT_TRUE(network_url.is_valid());

    url_loader_factory_.SetInterceptor(base::BindLambdaForTesting(
        [&, network_url](const network::ResourceRequest& request) {
          base::StringPiece request_string(request.request_body->elements()
                                               ->at(0)
                                               .As<network::DataElementBytes>()
                                               .AsStringPiece());
          url_loader_factory_.ClearResponses();
          if (request_string.find(GetFunctionHash("resolver(bytes32)")) !=
              std::string::npos) {
            url_loader_factory_.AddResponse(
                network_url.spec(),
                "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":"
                "\"0x0000000000000000000000004976fb03c32e5b8cfe2b6ccb31c09ba78e"
                "baba41\"}");
          } else if (request_string.find(GetFunctionHash(
                         "contenthash(bytes32)")) != std::string::npos) {
            url_loader_factory_.AddResponse(
                network_url.spec(),
                "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":"
                "\"0x0000000000000000000000000000000000000000000000000000000000"
                "00002000000000000000000000000000000000000000000000000000000000"
                "00000026e3010170122023e0160eec32d7875c19c5ac7c03bc1f306dc26008"
                "0d621454bc5f631e7310a70000000000000000000000000000000000000000"
                "000000000000\"}");
          } else if (request_string.find(GetFunctionHash("addr(bytes32)")) !=
                     std::string::npos) {
            url_loader_factory_.AddResponse(
                network_url.spec(),
                "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":"
                "\"0x000000000000000000000000983110309620d911731ac0932219af0609"
                "1b6744\"}");
          } else if (request_string.find(GetFunctionHash(
                         "get(string,uint256)")) != std::string::npos) {
            url_loader_factory_.AddResponse(
                network_url.spec(),
                "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":"
                "\"0x0000000000000000000000000000000000000000000000000000000000"
                "00002000000000000000000000000000000000000000000000000000000000"
                "0000002a307838616144343433323141383662313730383739643741323434"
                "63316538643336306339394464413800000000000000000000000000000000"
                "000000000000\"}");
          } else {
            url_loader_factory_.AddResponse(request.url.spec(), "",
                                            net::HTTP_REQUEST_TIMEOUT);
          }
        }));
  }

  void SetERC721MetadataInterceptor(
      const std::string& supports_interface_provider_response,
      const std::string& token_uri_provider_response = "",
      const std::string& metadata_response = "",
      net::HttpStatusCode supports_interface_status = net::HTTP_OK,
      net::HttpStatusCode token_uri_status = net::HTTP_OK,
      net::HttpStatusCode metadata_status = net::HTTP_OK) {
    url_loader_factory_.SetInterceptor(base::BindLambdaForTesting(
        [&, supports_interface_status, token_uri_status,
         metadata_status](const network::ResourceRequest& request) {
          url_loader_factory_.ClearResponses();
          if (request.method ==
              "POST") {  // An eth_call, either to supportsInterface or tokenURI
            base::StringPiece request_string(
                request.request_body->elements()
                    ->at(0)
                    .As<network::DataElementBytes>()
                    .AsStringPiece());
            bool is_supports_interface_req =
                request_string.find(GetFunctionHash(
                    "supportsInterface(bytes4)")) != std::string::npos;
            bool is_token_uri_req =
                request_string.find(GetFunctionHash("tokenURI(uint256)")) !=
                std::string::npos;
            if (is_supports_interface_req) {
              url_loader_factory_.AddResponse(
                  request.url.spec(), supports_interface_provider_response,
                  supports_interface_status);
              return;
            } else if (is_token_uri_req) {
              url_loader_factory_.AddResponse(request.url.spec(),
                                              token_uri_provider_response,
                                              token_uri_status);
              return;
            }
          } else {  // A HTTP GET to fetch the metadata json from the web
            VLOG(0) << "In the interceptor, metadata_response is: "
                    << metadata_response;
            url_loader_factory_.AddResponse(request.url.spec(),
                                            metadata_response, metadata_status);
            return;
          }

          url_loader_factory_.AddResponse(request.url.spec(), "",
                                          net::HTTP_REQUEST_TIMEOUT);
        }));
  }

  void SetInterceptor(const std::string& expected_method,
                      const std::string& expected_cache_header,
                      const std::string& content) {
    url_loader_factory_.SetInterceptor(base::BindLambdaForTesting(
        [&, expected_method, expected_cache_header,
         content](const network::ResourceRequest& request) {
          std::string header_value(100, '\0');
          EXPECT_TRUE(request.headers.GetHeader("x-brave-key", &header_value));
          EXPECT_EQ(BRAVE_SERVICES_KEY, header_value);
          EXPECT_TRUE(request.headers.GetHeader("X-Eth-Method", &header_value));
          EXPECT_EQ(expected_method, header_value);
          if (expected_method == "eth_blockNumber") {
            EXPECT_TRUE(
                request.headers.GetHeader("X-Eth-Block", &header_value));
            EXPECT_EQ(expected_cache_header, header_value);
          } else if (expected_method == "eth_getBlockByNumber") {
            EXPECT_TRUE(
                request.headers.GetHeader("X-eth-get-block", &header_value));
            EXPECT_EQ(expected_cache_header, header_value);
          }
          url_loader_factory_.ClearResponses();
          url_loader_factory_.AddResponse(request.url.spec(), content);
        }));
  }

  void SetInvalidJsonInterceptor() {
    url_loader_factory_.SetInterceptor(base::BindLambdaForTesting(
        [&](const network::ResourceRequest& request) {
          url_loader_factory_.ClearResponses();
          url_loader_factory_.AddResponse(request.url.spec(), "Answer is 42");
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

  void SetLimitExceededJsonErrorResponse() {
    url_loader_factory_.SetInterceptor(base::BindLambdaForTesting(
        [&](const network::ResourceRequest& request) {
          url_loader_factory_.ClearResponses();
          url_loader_factory_.AddResponse(request.url.spec(),
                                          R"({
            "jsonrpc":"2.0",
            "id":1,
            "error": {
              "code":-32005,
              "message": "Request exceeds defined limit"
            }
          })");
        }));
  }

  void SetIsEip1559Interceptor(bool is_eip1559) {
    if (is_eip1559)
      SetInterceptor(
          "eth_getBlockByNumber", "latest,false",
          "{\"jsonrpc\":\"2.0\",\"id\": \"0\",\"result\": "
          "{\"baseFeePerGas\":\"0x181f22e7a9\", \"gasLimit\":\"0x6691b8\"}}");
    else
      SetInterceptor("eth_getBlockByNumber", "latest,false",
                     "{\"jsonrpc\":\"2.0\",\"id\": \"0\",\"result\": "
                     "{\"gasLimit\":\"0x6691b8\"}}");
  }

  void ValidateStartWithNetwork(const std::string& chain_id,
                                const std::string& expected_id) {
    DictionaryPrefUpdate update(prefs(), kBraveWalletSelectedNetworks);
    base::Value* dict = update.Get();
    DCHECK(dict);
    dict->SetStringKey(kEthereumPrefKey, chain_id);
    JsonRpcService service(shared_url_loader_factory(), prefs());
    bool callback_is_called = false;
    service.GetChainId(
        mojom::CoinType::ETH,
        base::BindLambdaForTesting(
            [&callback_is_called, &expected_id](const std::string& chain_id) {
              EXPECT_EQ(chain_id, expected_id);
              callback_is_called = true;
            }));
    ASSERT_TRUE(callback_is_called);
  }

  bool SetNetwork(const std::string& chain_id, mojom::CoinType coin) {
    bool result;
    base::RunLoop run_loop;
    json_rpc_service_->SetNetwork(chain_id, coin,
                                  base::BindLambdaForTesting([&](bool success) {
                                    result = success;
                                    run_loop.Quit();
                                  }));
    run_loop.Run();
    return result;
  }

  void TestGetSolanaBalance(uint64_t expected_balance,
                            mojom::SolanaProviderError expected_error,
                            const std::string& expected_error_message) {
    base::RunLoop run_loop;
    json_rpc_service_->GetSolanaBalance(
        "test_public_key", mojom::kSolanaMainnet,
        base::BindLambdaForTesting([&](uint64_t balance,
                                       mojom::SolanaProviderError error,
                                       const std::string& error_message) {
          EXPECT_EQ(balance, expected_balance);
          EXPECT_EQ(error, expected_error);
          EXPECT_EQ(error_message, expected_error_message);
          run_loop.Quit();
        }));
    run_loop.Run();
  }

  void TestGetSPLTokenAccountBalance(
      const std::string& expected_amount,
      uint8_t expected_decimals,
      const std::string& expected_ui_amount_string,
      mojom::SolanaProviderError expected_error,
      const std::string& expected_error_message) {
    base::RunLoop run_loop;
    json_rpc_service_->GetSPLTokenAccountBalance(
        "BrG44HdsEhzapvs8bEqzvkq4egwevS3fRE6ze2ENo6S8",
        "AQoKYV7tYpTrFZN6P5oUufbQKAUr9mNYGe1TTJC9wajM", mojom::kSolanaMainnet,
        base::BindLambdaForTesting([&](const std::string& amount,
                                       uint8_t decimals,
                                       const std::string& ui_amount_string,
                                       mojom::SolanaProviderError error,
                                       const std::string& error_message) {
          EXPECT_EQ(amount, expected_amount);
          EXPECT_EQ(decimals, expected_decimals);
          EXPECT_EQ(ui_amount_string, expected_ui_amount_string);
          EXPECT_EQ(error, expected_error);
          EXPECT_EQ(error_message, expected_error_message);
          run_loop.Quit();
        }));
    run_loop.Run();
  }

  void TestSendSolanaTransaction(const std::string& expected_tx_id,
                                 mojom::SolanaProviderError expected_error,
                                 const std::string& expected_error_message) {
    base::RunLoop run_loop;
    json_rpc_service_->SendSolanaTransaction(
        "signed_tx",
        base::BindLambdaForTesting([&](const std::string& tx_id,
                                       mojom::SolanaProviderError error,
                                       const std::string& error_message) {
          EXPECT_EQ(tx_id, expected_tx_id);
          EXPECT_EQ(error, expected_error);
          EXPECT_EQ(error_message, expected_error_message);
          run_loop.Quit();
        }));
    run_loop.Run();
  }

  void TestGetSolanaLatestBlockhash(const std::string& expected_hash,
                                    mojom::SolanaProviderError expected_error,
                                    const std::string& expected_error_message) {
    base::RunLoop run_loop;
    json_rpc_service_->GetSolanaLatestBlockhash(base::BindLambdaForTesting(
        [&](const std::string& hash, mojom::SolanaProviderError error,
            const std::string& error_message) {
          EXPECT_EQ(hash, expected_hash);
          EXPECT_EQ(error, expected_error);
          EXPECT_EQ(error_message, expected_error_message);
          run_loop.Quit();
        }));
    run_loop.Run();
  }

  void TestGetSolanaSignatureStatuses(
      const std::vector<std::string>& tx_signatures,
      const std::vector<absl::optional<SolanaSignatureStatus>>& expected_stats,
      mojom::SolanaProviderError expected_error,
      const std::string& expected_error_message) {
    base::RunLoop run_loop;
    json_rpc_service_->GetSolanaSignatureStatuses(
        tx_signatures,
        base::BindLambdaForTesting(
            [&](const std::vector<absl::optional<SolanaSignatureStatus>>& stats,
                mojom::SolanaProviderError error,
                const std::string& error_message) {
              EXPECT_EQ(stats, expected_stats);
              EXPECT_EQ(error, expected_error);
              EXPECT_EQ(error_message, expected_error_message);
              run_loop.Quit();
            }));
    run_loop.Run();
  }

  void TestGetSolanaAccountInfo(
      absl::optional<SolanaAccountInfo> expected_account_info,
      mojom::SolanaProviderError expected_error,
      const std::string& expected_error_message) {
    base::RunLoop run_loop;
    json_rpc_service_->GetSolanaAccountInfo(
        "vines1vzrYbzLMRdu58ou5XTby4qAqVRLmqo36NKPTg",
        base::BindLambdaForTesting(
            [&](absl::optional<SolanaAccountInfo> account_info,
                mojom::SolanaProviderError error,
                const std::string& error_message) {
              EXPECT_EQ(account_info, expected_account_info);
              EXPECT_EQ(error, expected_error);
              EXPECT_EQ(error_message, expected_error_message);
              run_loop.Quit();
            }));
    run_loop.Run();
  }

 protected:
  std::unique_ptr<JsonRpcService> json_rpc_service_;

 private:
  base::test::TaskEnvironment task_environment_;
  sync_preferences::TestingPrefServiceSyncable prefs_;
  network::TestURLLoaderFactory url_loader_factory_;
  scoped_refptr<network::SharedURLLoaderFactory> shared_url_loader_factory_;
  data_decoder::test::InProcessDataDecoder in_process_data_decoder_;
};

TEST_F(JsonRpcServiceUnitTest, SetNetwork) {
  std::vector<mojom::NetworkInfoPtr> networks;
  brave_wallet::GetAllKnownEthChains(prefs(), &networks);
  for (const auto& network : networks) {
    bool callback_is_called = false;
    EXPECT_TRUE(SetNetwork(network->chain_id, mojom::CoinType::ETH));

    EXPECT_EQ(network->chain_id,
              GetCurrentChainId(prefs(), mojom::CoinType::ETH));
    const std::string& expected_id = network->chain_id;
    json_rpc_service_->GetChainId(
        mojom::CoinType::ETH,
        base::BindLambdaForTesting(
            [&callback_is_called, &expected_id](const std::string& chain_id) {
              EXPECT_EQ(chain_id, expected_id);
              callback_is_called = true;
            }));
    ASSERT_TRUE(callback_is_called);

    callback_is_called = false;
    const std::string& expected_url = network->rpc_urls.front();
    json_rpc_service_->GetNetworkUrl(
        mojom::CoinType::ETH,
        base::BindLambdaForTesting(
            [&callback_is_called, &expected_url](const std::string& spec) {
              EXPECT_EQ(url::Origin::Create(GURL(spec)),
                        url::Origin::Create(GURL(expected_url)));
              callback_is_called = true;
            }));
    ASSERT_TRUE(callback_is_called);
  }
  base::RunLoop().RunUntilIdle();

  // Solana
  ASSERT_EQ(mojom::kSolanaMainnet,
            GetCurrentChainId(prefs(), mojom::CoinType::SOL));
  EXPECT_FALSE(SetNetwork("0x1234", mojom::CoinType::SOL));
  EXPECT_TRUE(SetNetwork(mojom::kSolanaTestnet, mojom::CoinType::SOL));

  base::RunLoop run_loop;
  json_rpc_service_->GetChainId(
      mojom::CoinType::SOL,
      base::BindLambdaForTesting([&run_loop](const std::string& chain_id) {
        EXPECT_EQ(chain_id, mojom::kSolanaTestnet);
        run_loop.Quit();
      }));
  run_loop.Run();

  base::RunLoop run_loop2;
  json_rpc_service_->GetNetworkUrl(
      mojom::CoinType::SOL,
      base::BindLambdaForTesting([&run_loop2](const std::string& spec) {
        EXPECT_EQ(url::Origin::Create(GURL(spec)),
                  url::Origin::Create(GURL("https://api.testnet.solana.com")));
        run_loop2.Quit();
      }));
  run_loop2.Run();
}

TEST_F(JsonRpcServiceUnitTest, SetCustomNetwork) {
  std::vector<base::Value> values;
  mojom::NetworkInfo chain1("chain_id", "chain_name", {"https://url1.com"},
                            {"https://url1.com"}, {"https://url1.com"},
                            "symbol_name", "symbol", 11, mojom::CoinType::ETH,
                            mojom::NetworkInfoData::NewEthData(
                                mojom::NetworkInfoDataETH::New(false)));
  auto chain_ptr1 = chain1.Clone();
  values.push_back(EthNetworkInfoToValue(chain_ptr1));

  brave_wallet::mojom::NetworkInfo chain2(
      "chain_id2", "chain_name2", {"https://url2.com"}, {"https://url2.com"},
      {"https://url2.com"}, "symbol_name2", "symbol2", 22, mojom::CoinType::ETH,
      mojom::NetworkInfoData::NewEthData(mojom::NetworkInfoDataETH::New(true)));
  auto chain_ptr2 = chain2.Clone();
  values.push_back(EthNetworkInfoToValue(chain_ptr2));
  UpdateCustomNetworks(prefs(), &values);

  bool callback_is_called = false;
  EXPECT_TRUE(SetNetwork(chain1.chain_id, mojom::CoinType::ETH));
  const std::string& expected_id = chain1.chain_id;
  json_rpc_service_->GetChainId(
      mojom::CoinType::ETH,
      base::BindLambdaForTesting(
          [&callback_is_called, &expected_id](const std::string& chain_id) {
            EXPECT_EQ(chain_id, expected_id);
            callback_is_called = true;
          }));
  ASSERT_TRUE(callback_is_called);
  callback_is_called = false;
  const std::string& expected_url = chain1.rpc_urls.front();
  json_rpc_service_->GetNetworkUrl(
      mojom::CoinType::ETH,
      base::BindLambdaForTesting(
          [&callback_is_called, &expected_url](const std::string& spec) {
            EXPECT_EQ(url::Origin::Create(GURL(spec)),
                      url::Origin::Create(GURL(expected_url)));
            callback_is_called = true;
          }));
  ASSERT_TRUE(callback_is_called);
  base::RunLoop().RunUntilIdle();
}

TEST_F(JsonRpcServiceUnitTest, GetAllNetworks) {
  std::vector<base::Value> values;
  mojom::NetworkInfo chain1("chain_id", "chain_name", {"https://url1.com"},
                            {"https://url1.com"}, {"https://url1.com"},
                            "symbol_name", "symbol", 11, mojom::CoinType::ETH,
                            mojom::NetworkInfoData::NewEthData(
                                mojom::NetworkInfoDataETH::New(false)));
  auto chain_ptr1 = chain1.Clone();
  values.push_back(EthNetworkInfoToValue(chain_ptr1));

  mojom::NetworkInfo chain2(
      "chain_id2", "chain_name2", {"https://url2.com"}, {"https://url2.com"},
      {"https://url2.com"}, "symbol_name2", "symbol2", 22, mojom::CoinType::ETH,
      mojom::NetworkInfoData::NewEthData(mojom::NetworkInfoDataETH::New(true)));
  auto chain_ptr2 = chain2.Clone();
  values.push_back(EthNetworkInfoToValue(chain_ptr2));
  UpdateCustomNetworks(prefs(), &values);

  std::vector<mojom::NetworkInfoPtr> expected_chains;
  GetAllChains(prefs(), mojom::CoinType::ETH, &expected_chains);
  bool callback_is_called = false;
  json_rpc_service_->GetAllNetworks(
      mojom::CoinType::ETH,
      base::BindLambdaForTesting(
          [&callback_is_called,
           &expected_chains](std::vector<mojom::NetworkInfoPtr> chains) {
            EXPECT_EQ(expected_chains.size(), chains.size());

            for (size_t i = 0; i < chains.size(); i++) {
              ASSERT_TRUE(chains.at(i).Equals(expected_chains.at(i)));
            }
            callback_is_called = true;
          }));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(callback_is_called);

  callback_is_called = false;
  json_rpc_service_->GetAllNetworks(
      mojom::CoinType::SOL,
      base::BindLambdaForTesting(
          [&callback_is_called](std::vector<mojom::NetworkInfoPtr> chains) {
            EXPECT_EQ(chains.size(), 4u);

            callback_is_called = true;
          }));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(callback_is_called);
}

TEST_F(JsonRpcServiceUnitTest, EnsResolverGetContentHash) {
  // Non-support chain should fail.
  SetUDENSInterceptor(mojom::kLocalhostChainId);

  bool callback_called = false;
  json_rpc_service_->EnsResolverGetContentHash(
      mojom::kLocalhostChainId, "brantly.eth",
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kInvalidParams,
                     l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS),
                     ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  SetUDENSInterceptor(mojom::kMainnetChainId);
  json_rpc_service_->EnsResolverGetContentHash(
      mojom::kMainnetChainId, "brantly.eth",
      base::BindLambdaForTesting([&](const std::string& result,
                                     brave_wallet::mojom::ProviderError error,
                                     const std::string& error_message) {
        callback_called = true;
        EXPECT_EQ(error, mojom::ProviderError::kSuccess);
        EXPECT_TRUE(error_message.empty());
        EXPECT_EQ(
            ipfs::ContentHashToCIDv1URL(result).spec(),
            "ipfs://"
            "bafybeibd4ala53bs26dvygofvr6ahpa7gbw4eyaibvrbivf4l5rr44yqu4");
      }));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  SetHTTPRequestTimeoutInterceptor();
  json_rpc_service_->EnsResolverGetContentHash(
      mojom::kMainnetChainId, "brantly.eth",
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kInternalError,
                     l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR), ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  SetInvalidJsonInterceptor();
  json_rpc_service_->EnsResolverGetContentHash(
      mojom::kMainnetChainId, "brantly.eth",
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kParsingError,
                     l10n_util::GetStringUTF8(IDS_WALLET_PARSING_ERROR), ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  SetLimitExceededJsonErrorResponse();
  json_rpc_service_->EnsResolverGetContentHash(
      mojom::kMainnetChainId, "brantly.eth",
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kLimitExceeded,
                     "Request exceeds defined limit", ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);
}

TEST_F(JsonRpcServiceUnitTest, EnsGetEthAddr) {
  // Non-support chain (localhost) should fail.
  SetUDENSInterceptor(json_rpc_service_->GetChainId(mojom::CoinType::ETH));
  bool callback_called = false;
  json_rpc_service_->EnsGetEthAddr(
      "brantly.eth",
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kInvalidParams,
                     l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS),
                     ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  EXPECT_TRUE(SetNetwork(mojom::kMainnetChainId, mojom::CoinType::ETH));
  SetUDENSInterceptor(mojom::kMainnetChainId);
  json_rpc_service_->EnsGetEthAddr(
      "brantly-test.eth",
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kSuccess, "",
                     "0x983110309620D911731Ac0932219af06091b6744"));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);
}

TEST_F(JsonRpcServiceUnitTest, AddEthereumChainApproved) {
  mojom::NetworkInfo chain("0x111", "chain_name", {"https://url1.com"},
                           {"https://url1.com"}, {"https://url1.com"}, "symbol",
                           "symbol_name", 11, mojom::CoinType::ETH,
                           mojom::NetworkInfoData::NewEthData(
                               mojom::NetworkInfoDataETH::New(false)));

  bool callback_is_called = false;
  mojom::ProviderError expected = mojom::ProviderError::kSuccess;
  ASSERT_FALSE(
      brave_wallet::GetNetworkURL(prefs(), "0x111", mojom::CoinType::ETH)
          .is_valid());
  SetEthChainIdInterceptor(chain.rpc_urls.front(), "0x111");
  json_rpc_service_->AddEthereumChain(
      chain.Clone(),
      base::BindLambdaForTesting(
          [&callback_is_called, &expected](const std::string& chain_id,
                                           mojom::ProviderError error,
                                           const std::string& error_message) {
            ASSERT_FALSE(chain_id.empty());
            EXPECT_EQ(error, expected);
            ASSERT_TRUE(error_message.empty());
            callback_is_called = true;
          }));
  base::RunLoop().RunUntilIdle();

  bool failed_callback_is_called = false;
  mojom::ProviderError expected_error =
      mojom::ProviderError::kUserRejectedRequest;
  json_rpc_service_->AddEthereumChain(
      chain.Clone(),
      base::BindLambdaForTesting([&failed_callback_is_called, &expected_error](
                                     const std::string& chain_id,
                                     mojom::ProviderError error,
                                     const std::string& error_message) {
        ASSERT_FALSE(chain_id.empty());
        EXPECT_EQ(error, expected_error);
        ASSERT_FALSE(error_message.empty());
        failed_callback_is_called = true;
      }));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(failed_callback_is_called);

  json_rpc_service_->AddEthereumChainRequestCompleted("0x111", true);

  ASSERT_TRUE(callback_is_called);
  ASSERT_TRUE(
      brave_wallet::GetNetworkURL(prefs(), "0x111", mojom::CoinType::ETH)
          .is_valid());

  // Prefs should be updated.
  std::vector<brave_wallet::mojom::NetworkInfoPtr> custom_chains;
  GetAllEthCustomChains(prefs(), &custom_chains);
  ASSERT_EQ(custom_chains.size(), 1u);
  EXPECT_EQ(custom_chains[0], chain.Clone());

  const base::Value* assets_pref =
      prefs()->GetDictionary(kBraveWalletUserAssets);
  const base::Value* list = assets_pref->FindKey("0x111");
  ASSERT_TRUE(list->is_list());
  const base::Value::List& asset_list = list->GetList();
  ASSERT_EQ(asset_list.size(), 1u);

  EXPECT_EQ(*asset_list[0].FindStringKey("contract_address"), "");
  EXPECT_EQ(*asset_list[0].FindStringKey("name"), "symbol_name");
  EXPECT_EQ(*asset_list[0].FindStringKey("symbol"), "symbol");
  EXPECT_EQ(*asset_list[0].FindBoolKey("is_erc20"), false);
  EXPECT_EQ(*asset_list[0].FindBoolKey("is_erc721"), false);
  EXPECT_EQ(*asset_list[0].FindIntKey("decimals"), 11);
  EXPECT_EQ(*asset_list[0].FindStringKey("logo"), "https://url1.com");
  EXPECT_EQ(*asset_list[0].FindBoolKey("visible"), true);

  callback_is_called = false;
  json_rpc_service_->AddEthereumChainRequestCompleted("0x111", true);
  ASSERT_FALSE(callback_is_called);
}

TEST_F(JsonRpcServiceUnitTest, AddEthereumChainApprovedForOrigin) {
  mojom::NetworkInfo chain("0x111", "chain_name", {"https://url1.com"},
                           {"https://url1.com"}, {"https://url1.com"}, "symbol",
                           "symbol_name", 11, mojom::CoinType::ETH,
                           mojom::NetworkInfoData::NewEthData(
                               mojom::NetworkInfoDataETH::New(false)));

  base::RunLoop loop;
  std::unique_ptr<TestJsonRpcServiceObserver> observer(
      new TestJsonRpcServiceObserver(loop.QuitClosure(), "0x111",
                                     mojom::CoinType::ETH, true));

  json_rpc_service_->AddObserver(observer->GetReceiver());

  mojo::PendingRemote<brave_wallet::mojom::JsonRpcServiceObserver> receiver;
  mojo::MakeSelfOwnedReceiver(std::move(observer),
                              receiver.InitWithNewPipeAndPassReceiver());

  bool callback_is_called = false;
  mojom::ProviderError expected = mojom::ProviderError::kSuccess;
  ASSERT_FALSE(
      brave_wallet::GetNetworkURL(prefs(), "0x111", mojom::CoinType::ETH)
          .is_valid());
  SetEthChainIdInterceptor(chain.rpc_urls.front(), "0x111");
  json_rpc_service_->AddEthereumChainForOrigin(
      chain.Clone(), GURL("https://brave.com"),
      base::BindLambdaForTesting(
          [&callback_is_called, &expected](const std::string& chain_id,
                                           mojom::ProviderError error,
                                           const std::string& error_message) {
            ASSERT_FALSE(chain_id.empty());
            EXPECT_EQ(error, expected);
            ASSERT_TRUE(error_message.empty());
            callback_is_called = true;
          }));
  base::RunLoop().RunUntilIdle();
  json_rpc_service_->AddEthereumChainRequestCompleted("0x111", true);
  loop.Run();

  ASSERT_TRUE(callback_is_called);
  ASSERT_TRUE(
      brave_wallet::GetNetworkURL(prefs(), "0x111", mojom::CoinType::ETH)
          .is_valid());

  // Prefs should be updated.
  std::vector<brave_wallet::mojom::NetworkInfoPtr> custom_chains;
  GetAllEthCustomChains(prefs(), &custom_chains);
  ASSERT_EQ(custom_chains.size(), 1u);
  EXPECT_EQ(custom_chains[0], chain.Clone());

  const base::Value* assets_pref =
      prefs()->GetDictionary(kBraveWalletUserAssets);
  const base::Value* list = assets_pref->FindKey("0x111");
  ASSERT_TRUE(list->is_list());
  const base::Value::List& asset_list = list->GetList();
  ASSERT_EQ(asset_list.size(), 1u);

  EXPECT_EQ(*asset_list[0].FindStringKey("contract_address"), "");
  EXPECT_EQ(*asset_list[0].FindStringKey("name"), "symbol_name");
  EXPECT_EQ(*asset_list[0].FindStringKey("symbol"), "symbol");
  EXPECT_EQ(*asset_list[0].FindBoolKey("is_erc20"), false);
  EXPECT_EQ(*asset_list[0].FindBoolKey("is_erc721"), false);
  EXPECT_EQ(*asset_list[0].FindIntKey("decimals"), 11);
  EXPECT_EQ(*asset_list[0].FindStringKey("logo"), "https://url1.com");
  EXPECT_EQ(*asset_list[0].FindBoolKey("visible"), true);

  callback_is_called = false;
  json_rpc_service_->AddEthereumChainRequestCompleted("0x111", true);
  ASSERT_FALSE(callback_is_called);
}

TEST_F(JsonRpcServiceUnitTest, AddEthereumChainRejected) {
  mojom::NetworkInfo chain("0x111", "chain_name", {"https://url1.com"},
                           {"https://url1.com"}, {"https://url1.com"},
                           "symbol_name", "symbol", 11, mojom::CoinType::ETH,
                           mojom::NetworkInfoData::NewEthData(
                               mojom::NetworkInfoDataETH::New(false)));

  base::RunLoop loop;
  std::unique_ptr<TestJsonRpcServiceObserver> observer(
      new TestJsonRpcServiceObserver(loop.QuitClosure(), "0x111",
                                     mojom::CoinType::ETH, false));

  json_rpc_service_->AddObserver(observer->GetReceiver());

  mojo::PendingRemote<brave_wallet::mojom::JsonRpcServiceObserver> receiver;
  mojo::MakeSelfOwnedReceiver(std::move(observer),
                              receiver.InitWithNewPipeAndPassReceiver());

  bool callback_is_called = false;
  mojom::ProviderError expected = mojom::ProviderError::kSuccess;
  ASSERT_FALSE(
      brave_wallet::GetNetworkURL(prefs(), "0x111", mojom::CoinType::ETH)
          .is_valid());
  SetEthChainIdInterceptor(chain.rpc_urls.front(), "0x111");
  json_rpc_service_->AddEthereumChainForOrigin(
      chain.Clone(), GURL("https://brave.com"),
      base::BindLambdaForTesting(
          [&callback_is_called, &expected](const std::string& chain_id,
                                           mojom::ProviderError error,
                                           const std::string& error_message) {
            ASSERT_FALSE(chain_id.empty());
            EXPECT_EQ(error, expected);
            ASSERT_TRUE(error_message.empty());
            callback_is_called = true;
          }));
  base::RunLoop().RunUntilIdle();
  json_rpc_service_->AddEthereumChainRequestCompleted("0x111", false);
  loop.Run();
  ASSERT_TRUE(callback_is_called);
  ASSERT_FALSE(
      brave_wallet::GetNetworkURL(prefs(), "0x111", mojom::CoinType::ETH)
          .is_valid());
  callback_is_called = false;
  json_rpc_service_->AddEthereumChainRequestCompleted("0x111", true);
  ASSERT_FALSE(callback_is_called);
  ASSERT_FALSE(
      brave_wallet::GetNetworkURL(prefs(), "0x111", mojom::CoinType::ETH)
          .is_valid());
}

TEST_F(JsonRpcServiceUnitTest, AddEthereumChainError) {
  mojom::NetworkInfo chain("0x111", "chain_name", {"https://url1.com"},
                           {"https://url1.com"}, {"https://url1.com"},
                           "symbol_name", "symbol", 11, mojom::CoinType::ETH,
                           mojom::NetworkInfoData::NewEthData(
                               mojom::NetworkInfoDataETH::New(false)));

  bool callback_is_called = false;
  mojom::ProviderError expected = mojom::ProviderError::kSuccess;
  ASSERT_FALSE(
      brave_wallet::GetNetworkURL(prefs(), chain.chain_id, mojom::CoinType::ETH)
          .is_valid());
  SetEthChainIdInterceptor(chain.rpc_urls.front(), chain.chain_id);
  json_rpc_service_->AddEthereumChainForOrigin(
      chain.Clone(), GURL("https://brave.com"),
      base::BindLambdaForTesting(
          [&callback_is_called, &expected](const std::string& chain_id,
                                           mojom::ProviderError error,
                                           const std::string& error_message) {
            ASSERT_FALSE(chain_id.empty());
            EXPECT_EQ(error, expected);
            ASSERT_TRUE(error_message.empty());
            callback_is_called = true;
          }));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(callback_is_called);
  callback_is_called = false;

  // other chain, same origin
  mojom::NetworkInfo chain2("0x222", "chain_name", {"https://url1.com"},
                            {"https://url1.com"}, {"https://url1.com"},
                            "symbol_name", "symbol", 11, mojom::CoinType::ETH,
                            mojom::NetworkInfoData::NewEthData(
                                mojom::NetworkInfoDataETH::New(false)));

  bool second_callback_is_called = false;
  mojom::ProviderError second_expected =
      mojom::ProviderError::kUserRejectedRequest;
  SetEthChainIdInterceptor(chain2.rpc_urls.front(), chain2.chain_id);
  json_rpc_service_->AddEthereumChainForOrigin(
      chain2.Clone(), GURL("https://brave.com"),
      base::BindLambdaForTesting([&second_callback_is_called, &second_expected](
                                     const std::string& chain_id,
                                     mojom::ProviderError error,
                                     const std::string& error_message) {
        ASSERT_FALSE(chain_id.empty());
        EXPECT_EQ(error, second_expected);
        EXPECT_EQ(error_message, l10n_util::GetStringUTF8(
                                     IDS_WALLET_ALREADY_IN_PROGRESS_ERROR));
        second_callback_is_called = true;
      }));
  base::RunLoop().RunUntilIdle();
  ASSERT_FALSE(callback_is_called);
  ASSERT_TRUE(second_callback_is_called);
  second_callback_is_called = false;

  // same chain, other origin
  bool third_callback_is_called = false;
  mojom::ProviderError third_expected =
      mojom::ProviderError::kUserRejectedRequest;
  json_rpc_service_->AddEthereumChainForOrigin(
      chain.Clone(), GURL("https://others.com"),
      base::BindLambdaForTesting([&third_callback_is_called, &third_expected](
                                     const std::string& chain_id,
                                     mojom::ProviderError error,
                                     const std::string& error_message) {
        ASSERT_FALSE(chain_id.empty());
        EXPECT_EQ(error, third_expected);
        EXPECT_EQ(error_message, l10n_util::GetStringUTF8(
                                     IDS_WALLET_ALREADY_IN_PROGRESS_ERROR));
        third_callback_is_called = true;
      }));
  base::RunLoop().RunUntilIdle();
  ASSERT_FALSE(callback_is_called);
  ASSERT_FALSE(second_callback_is_called);
  ASSERT_TRUE(third_callback_is_called);

  // new chain, not valid rpc url
  mojom::NetworkInfo chain4("0x444", "chain_name4", {"https://url4.com"},
                            {"https://url4.com"}, {"https://url4.com"},
                            "symbol_name", "symbol", 11, mojom::CoinType::ETH,
                            mojom::NetworkInfoData::NewEthData(
                                mojom::NetworkInfoDataETH::New(false)));
  bool fourth_callback_is_called = false;
  mojom::ProviderError fourth_expected =
      mojom::ProviderError::kUserRejectedRequest;
  auto network_url = chain4.rpc_urls.front();
  SetEthChainIdInterceptor(chain4.rpc_urls.front(), "0x555");
  json_rpc_service_->AddEthereumChainForOrigin(
      chain4.Clone(), GURL("https://others4.com"),
      base::BindLambdaForTesting(
          [&fourth_callback_is_called, &fourth_expected, &network_url](
              const std::string& chain_id, mojom::ProviderError error,
              const std::string& error_message) {
            ASSERT_FALSE(chain_id.empty());
            EXPECT_EQ(error, fourth_expected);
            EXPECT_EQ(error_message,
                      l10n_util::GetStringFUTF8(
                          IDS_BRAVE_WALLET_ETH_CHAIN_ID_FAILED,
                          base::ASCIIToUTF16(GURL(network_url).spec())));
            fourth_callback_is_called = true;
          }));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(fourth_callback_is_called);

  // new chain, broken validation response
  mojom::NetworkInfo chain5("0x444", "chain_name5", {"https://url5.com"},
                            {"https://url5.com"}, {"https://url5.com"},
                            "symbol_name", "symbol", 11, mojom::CoinType::ETH,
                            mojom::NetworkInfoData::NewEthData(
                                mojom::NetworkInfoDataETH::New(false)));
  bool fifth_callback_is_called = false;
  mojom::ProviderError fifth_expected =
      mojom::ProviderError::kUserRejectedRequest;
  network_url = chain5.rpc_urls.front();
  SetEthChainIdInterceptorWithBrokenResponse(chain5.rpc_urls.front());
  json_rpc_service_->AddEthereumChainForOrigin(
      chain5.Clone(), GURL("https://others5.com"),
      base::BindLambdaForTesting(
          [&fifth_callback_is_called, &fifth_expected, &network_url](
              const std::string& chain_id, mojom::ProviderError error,
              const std::string& error_message) {
            ASSERT_FALSE(chain_id.empty());
            EXPECT_EQ(error, fifth_expected);
            EXPECT_EQ(error_message,
                      l10n_util::GetStringFUTF8(
                          IDS_BRAVE_WALLET_ETH_CHAIN_ID_FAILED,
                          base::ASCIIToUTF16(GURL(network_url).spec())));
            fifth_callback_is_called = true;
          }));
  base::RunLoop().RunUntilIdle();
  ASSERT_TRUE(fifth_callback_is_called);
}

TEST_F(JsonRpcServiceUnitTest, StartWithNetwork) {
  ValidateStartWithNetwork(std::string(), std::string());
  ValidateStartWithNetwork("SomeBadChainId", std::string());
  ValidateStartWithNetwork(brave_wallet::mojom::kRopstenChainId,
                           brave_wallet::mojom::kRopstenChainId);
}

TEST_F(JsonRpcServiceUnitTest, Request) {
  bool callback_called = false;
  std::string request =
      "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"eth_blockNumber\",\"params\":"
      "[]}";
  std::string result = "\"0xb539d5\"";
  std::string expected_response =
      "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":" + result + "}";
  SetInterceptor("eth_blockNumber", "true", expected_response);
  json_rpc_service_->Request(
      request, true, base::Value(), mojom::CoinType::ETH,
      base::BindOnce(&OnRequestResponse, &callback_called, true /* success */,
                     result));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  request =
      "{\"jsonrpc\":\"2.0\",\"id\":1,\"method\":\"eth_getBlockByNumber\","
      "\"params\":"
      "[\"0x5BAD55\",true]}";
  result = "\"0xb539d5\"";
  expected_response =
      "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":" + result + "}";
  SetInterceptor("eth_getBlockByNumber", "0x5BAD55,true", expected_response);
  json_rpc_service_->Request(
      request, true, base::Value(), mojom::CoinType::ETH,
      base::BindOnce(&OnRequestResponse, &callback_called, true /* success */,
                     result));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  SetHTTPRequestTimeoutInterceptor();
  json_rpc_service_->Request(
      request, true, base::Value(), mojom::CoinType::ETH,
      base::BindOnce(&OnRequestResponse, &callback_called, false /* success */,
                     ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);
}

TEST_F(JsonRpcServiceUnitTest, GetBalance) {
  bool callback_called = false;
  SetInterceptor("eth_getBalance", "",
                 "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":\"0xb539d5\"}");
  json_rpc_service_->GetBalance(
      "0x4e02f254184E904300e0775E4b8eeCB1", mojom::CoinType::ETH,
      mojom::kMainnetChainId,
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kSuccess, "", "0xb539d5"));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  SetHTTPRequestTimeoutInterceptor();
  json_rpc_service_->GetBalance(
      "0x4e02f254184E904300e0775E4b8eeCB1", mojom::CoinType::ETH,
      mojom::kMainnetChainId,
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kInternalError,
                     l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR), ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  SetInvalidJsonInterceptor();
  json_rpc_service_->GetBalance(
      "0x4e02f254184E904300e0775E4b8eeCB1", mojom::CoinType::ETH,
      mojom::kMainnetChainId,
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kParsingError,
                     l10n_util::GetStringUTF8(IDS_WALLET_PARSING_ERROR), ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  json_rpc_service_->GetBalance(
      "0x4e02f254184E904300e0775E4b8eeCB1", mojom::CoinType::ETH, "",
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kInvalidParams,
                     l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS),
                     ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  SetLimitExceededJsonErrorResponse();
  json_rpc_service_->GetBalance(
      "0x4e02f254184E904300e0775E4b8eeCB1", mojom::CoinType::ETH,
      mojom::kMainnetChainId,
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kLimitExceeded,
                     "Request exceeds defined limit", ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);
}

TEST_F(JsonRpcServiceUnitTest, GetFeeHistory) {
  std::string json =
      R"(
      {
        "jsonrpc":"2.0",
        "id":1,
        "result": {
          "baseFeePerGas": [
            "0x215d00b8c8",
            "0x24beaded75"
          ],
          "gasUsedRatio": [
            0.020687709938714324
          ],
          "oldestBlock": "0xd6b1b0",
          "reward": [
            [
              "0x77359400",
              "0x77359400",
              "0x2816a6cfb"
            ]
          ]
        }
      })";

  SetInterceptor("eth_feeHistory", "", json);
  base::RunLoop run_loop;
  json_rpc_service_->GetFeeHistory(base::BindLambdaForTesting(
      [&](const std::vector<std::string>& base_fee_per_gas,
          const std::vector<double>& gas_used_ratio,
          const std::string& oldest_block,
          const std::vector<std::vector<std::string>>& reward,
          mojom::ProviderError error, const std::string& error_message) {
        EXPECT_EQ(error, mojom::ProviderError::kSuccess);
        EXPECT_TRUE(error_message.empty());
        EXPECT_EQ(base_fee_per_gas,
                  (std::vector<std::string>{"0x215d00b8c8", "0x24beaded75"}));
        EXPECT_EQ(gas_used_ratio, (std::vector<double>{0.020687709938714324}));
        EXPECT_EQ(oldest_block, "0xd6b1b0");
        EXPECT_EQ(reward, (std::vector<std::vector<std::string>>{
                              {"0x77359400", "0x77359400", "0x2816a6cfb"}}));
        run_loop.Quit();
      }));
  run_loop.Run();

  SetHTTPRequestTimeoutInterceptor();
  base::RunLoop run_loop2;
  json_rpc_service_->GetFeeHistory(base::BindLambdaForTesting(
      [&](const std::vector<std::string>& base_fee_per_gas,
          const std::vector<double>& gas_used_ratio,
          const std::string& oldest_block,
          const std::vector<std::vector<std::string>>& reward,
          mojom::ProviderError error, const std::string& error_message) {
        EXPECT_EQ(error, mojom::ProviderError::kInternalError);
        EXPECT_EQ(error_message,
                  l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR));
        run_loop2.Quit();
      }));
  run_loop2.Run();

  SetInvalidJsonInterceptor();
  base::RunLoop run_loop3;
  json_rpc_service_->GetFeeHistory(base::BindLambdaForTesting(
      [&](const std::vector<std::string>& base_fee_per_gas,
          const std::vector<double>& gas_used_ratio,
          const std::string& oldest_block,
          const std::vector<std::vector<std::string>>& reward,
          mojom::ProviderError error, const std::string& error_message) {
        EXPECT_EQ(error, mojom::ProviderError::kParsingError);
        EXPECT_EQ(error_message,
                  l10n_util::GetStringUTF8(IDS_WALLET_PARSING_ERROR));
        run_loop3.Quit();
      }));
  run_loop3.Run();

  SetLimitExceededJsonErrorResponse();
  base::RunLoop run_loop4;
  json_rpc_service_->GetFeeHistory(base::BindLambdaForTesting(
      [&](const std::vector<std::string>& base_fee_per_gas,
          const std::vector<double>& gas_used_ratio,
          const std::string& oldest_block,
          const std::vector<std::vector<std::string>>& reward,
          mojom::ProviderError error, const std::string& error_message) {
        EXPECT_EQ(error, mojom::ProviderError::kLimitExceeded);
        EXPECT_EQ(error_message, "Request exceeds defined limit");
        run_loop4.Quit();
      }));
  run_loop4.Run();
}

TEST_F(JsonRpcServiceUnitTest, GetERC20TokenBalance) {
  bool callback_called = false;
  SetInterceptor(
      "eth_call", "",
      "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":"
      "\"0x00000000000000000000000000000000000000000000000166e12cfce39a0000\""
      "}");

  json_rpc_service_->GetERC20TokenBalance(
      "0x0d8775f648430679a709e98d2b0cb6250d2887ef",
      "0x4e02f254184E904300e0775E4b8eeCB1", mojom::kMainnetChainId,
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kSuccess, "",
                     "0x00000000000000000000000000000000000000000000000166e12cf"
                     "ce39a0000"));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  SetHTTPRequestTimeoutInterceptor();
  json_rpc_service_->GetERC20TokenBalance(
      "0x0d8775f648430679a709e98d2b0cb6250d2887ef",
      "0x4e02f254184E904300e0775E4b8eeCB1", mojom::kMainnetChainId,
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kInternalError,
                     l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR), ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  SetInvalidJsonInterceptor();
  json_rpc_service_->GetERC20TokenBalance(
      "0x0d8775f648430679a709e98d2b0cb6250d2887ef",
      "0x4e02f254184E904300e0775E4b8eeCB1", mojom::kMainnetChainId,
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kParsingError,
                     l10n_util::GetStringUTF8(IDS_WALLET_PARSING_ERROR), ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  SetLimitExceededJsonErrorResponse();
  json_rpc_service_->GetERC20TokenBalance(
      "0x0d8775f648430679a709e98d2b0cb6250d2887ef",
      "0x4e02f254184E904300e0775E4b8eeCB1", mojom::kMainnetChainId,
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kLimitExceeded,
                     "Request exceeds defined limit", ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  // Invalid input should fail.
  callback_called = false;
  json_rpc_service_->GetERC20TokenBalance(
      "", "", mojom::kMainnetChainId,
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kInvalidParams,
                     l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS),
                     ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  json_rpc_service_->GetERC20TokenBalance(
      "0x0d8775f648430679a709e98d2b0cb6250d2887ef",
      "0x4e02f254184E904300e0775E4b8eeCB1", "",
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kInvalidParams,
                     l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS),
                     ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);
}

TEST_F(JsonRpcServiceUnitTest, GetERC20TokenAllowance) {
  bool callback_called = false;
  SetInterceptor(
      "eth_call", "",
      "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":"
      "\"0x00000000000000000000000000000000000000000000000166e12cfce39a0000\""
      "}");

  json_rpc_service_->GetERC20TokenAllowance(
      "0x0d8775f648430679a709e98d2b0cb6250d2887ef",
      "0xBFb30a082f650C2A15D0632f0e87bE4F8e64460f",
      "0xBFb30a082f650C2A15D0632f0e87bE4F8e64460a",
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kSuccess, "",
                     "0x00000000000000000000000000000000000000000000000166e12cf"
                     "ce39a0000"));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  SetHTTPRequestTimeoutInterceptor();
  json_rpc_service_->GetERC20TokenAllowance(
      "0x0d8775f648430679a709e98d2b0cb6250d2887ef",
      "0xBFb30a082f650C2A15D0632f0e87bE4F8e64460f",
      "0xBFb30a082f650C2A15D0632f0e87bE4F8e64460a",
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kInternalError,
                     l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR), ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  SetInvalidJsonInterceptor();
  json_rpc_service_->GetERC20TokenAllowance(
      "0x0d8775f648430679a709e98d2b0cb6250d2887ef",
      "0xBFb30a082f650C2A15D0632f0e87bE4F8e64460f",
      "0xBFb30a082f650C2A15D0632f0e87bE4F8e64460a",
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kParsingError,
                     l10n_util::GetStringUTF8(IDS_WALLET_PARSING_ERROR), ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  SetLimitExceededJsonErrorResponse();
  json_rpc_service_->GetERC20TokenAllowance(
      "0x0d8775f648430679a709e98d2b0cb6250d2887ef",
      "0xBFb30a082f650C2A15D0632f0e87bE4F8e64460f",
      "0xBFb30a082f650C2A15D0632f0e87bE4F8e64460a",
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kLimitExceeded,
                     "Request exceeds defined limit", ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  // Invalid input should fail.
  callback_called = false;
  json_rpc_service_->GetERC20TokenAllowance(
      "", "", "",
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kInvalidParams,
                     l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS),
                     ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);
}

TEST_F(JsonRpcServiceUnitTest, UnstoppableDomainsProxyReaderGetMany) {
  bool callback_called = false;
  SetInterceptor(
      "eth_call", "",
      "{\"jsonrpc\":\"2.0\",\"id\": \"0\",\"result\": "
      // offset for array
      "\"0x0000000000000000000000000000000000000000000000000000000000000020"
      // count for array
      "0000000000000000000000000000000000000000000000000000000000000006"
      // offsets for array elements
      "00000000000000000000000000000000000000000000000000000000000000c0"
      "0000000000000000000000000000000000000000000000000000000000000120"
      "0000000000000000000000000000000000000000000000000000000000000180"
      "00000000000000000000000000000000000000000000000000000000000001a0"
      "00000000000000000000000000000000000000000000000000000000000001c0"
      "0000000000000000000000000000000000000000000000000000000000000200"
      // count for "QmWrdNJWMbvRxxzLhojVKaBDswS4KNVM7LvjsN7QbDrvka"
      "000000000000000000000000000000000000000000000000000000000000002e"
      // encoding for "QmWrdNJWMbvRxxzLhojVKaBDswS4KNVM7LvjsN7QbDrvka"
      "516d5772644e4a574d62765278787a4c686f6a564b614244737753344b4e564d"
      "374c766a734e3751624472766b61000000000000000000000000000000000000"
      // count for "QmbWqxBEKC3P8tqsKc98xmWNzrzDtRLMiMPL8wBuTGsMnR"
      "000000000000000000000000000000000000000000000000000000000000002e"
      // encoding for "QmbWqxBEKC3P8tqsKc98xmWNzrzDtRLMiMPL8wBuTGsMnR"
      "516d6257717842454b433350387471734b633938786d574e7a727a4474524c4d"
      "694d504c387742755447734d6e52000000000000000000000000000000000000"
      // count for empty dns.A
      "0000000000000000000000000000000000000000000000000000000000000000"
      // count for empty dns.AAAA
      "0000000000000000000000000000000000000000000000000000000000000000"
      // count for "https://fallback1.test.com"
      "000000000000000000000000000000000000000000000000000000000000001a"
      // encoding for "https://fallback1.test.com"
      "68747470733a2f2f66616c6c6261636b312e746573742e636f6d000000000000"
      // count for "https://fallback2.test.com"
      "000000000000000000000000000000000000000000000000000000000000001a"
      // encoding for "https://fallback2.test.com"
      "68747470733a2f2f66616c6c6261636b322e746573742e636f6d000000000000\"}");

  std::vector<std::string> expected_values = {
      "QmWrdNJWMbvRxxzLhojVKaBDswS4KNVM7LvjsN7QbDrvka",
      "QmbWqxBEKC3P8tqsKc98xmWNzrzDtRLMiMPL8wBuTGsMnR",
      "",
      "",
      "https://fallback1.test.com",
      "https://fallback2.test.com"};

  json_rpc_service_->UnstoppableDomainsProxyReaderGetMany(
      mojom::kMainnetChainId, "brave.crypto" /* domain */,
      {"dweb.ipfs.hash", "ipfs.html.value", "browser.redirect_url",
       "ipfs.redirect_domain.value"} /* keys */,
      base::BindOnce(&OnStringsResponse, &callback_called,
                     mojom::ProviderError::kSuccess, "", expected_values));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  SetHTTPRequestTimeoutInterceptor();
  json_rpc_service_->UnstoppableDomainsProxyReaderGetMany(
      mojom::kMainnetChainId, "brave.crypto" /* domain */,
      {"dweb.ipfs.hash", "ipfs.html.value", "browser.redirect_url",
       "ipfs.redirect_domain.value"} /* keys */,
      base::BindOnce(&OnStringsResponse, &callback_called,
                     mojom::ProviderError::kInternalError,
                     l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR),
                     std::vector<std::string>()));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  SetInvalidJsonInterceptor();
  json_rpc_service_->UnstoppableDomainsProxyReaderGetMany(
      mojom::kMainnetChainId, "brave.crypto" /* domain */,
      {"dweb.ipfs.hash", "ipfs.html.value", "browser.redirect_url",
       "ipfs.redirect_domain.value"} /* keys */,
      base::BindOnce(&OnStringsResponse, &callback_called,
                     mojom::ProviderError::kParsingError,
                     l10n_util::GetStringUTF8(IDS_WALLET_PARSING_ERROR),
                     std::vector<std::string>()));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  SetLimitExceededJsonErrorResponse();
  json_rpc_service_->UnstoppableDomainsProxyReaderGetMany(
      mojom::kMainnetChainId, "brave.crypto" /* domain */,
      {"dweb.ipfs.hash", "ipfs.html.value", "browser.redirect_url",
       "ipfs.redirect_domain.value"} /* keys */,
      base::BindOnce(&OnStringsResponse, &callback_called,
                     mojom::ProviderError::kLimitExceeded,
                     "Request exceeds defined limit",
                     std::vector<std::string>()));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  json_rpc_service_->UnstoppableDomainsProxyReaderGetMany(
      "", "", std::vector<std::string>(),
      base::BindOnce(&OnStringsResponse, &callback_called,
                     mojom::ProviderError::kInvalidParams,
                     l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS),
                     std::vector<std::string>()));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);
}

TEST_F(JsonRpcServiceUnitTest, UnstoppableDomainsGetEthAddr) {
  // Non-support chain (localhost) should fail.
  SetUDENSInterceptor(json_rpc_service_->GetChainId(mojom::CoinType::ETH));
  bool callback_called = false;
  json_rpc_service_->UnstoppableDomainsGetEthAddr(
      "brad.crypto",
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kInvalidParams,
                     l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS),
                     ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  EXPECT_TRUE(SetNetwork(mojom::kMainnetChainId, mojom::CoinType::ETH));
  SetUDENSInterceptor(mojom::kMainnetChainId);
  json_rpc_service_->UnstoppableDomainsGetEthAddr(
      "brad-test.crypto",
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kSuccess, "",
                     "0x8aaD44321A86b170879d7A244c1e8d360c99DdA8"));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  // Return false if getting empty address result for non-exist domains.
  callback_called = false;
  SetInterceptor(
      "eth_call", "",
      "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":"
      "\"0x0000000000000000000000000000000000000000000000000000000000000020"
      "0000000000000000000000000000000000000000000000000000000000000000\"}");
  json_rpc_service_->UnstoppableDomainsGetEthAddr(
      "non-exist.crypto",
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kParsingError,
                     l10n_util::GetStringUTF8(IDS_WALLET_PARSING_ERROR), ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);
}

TEST_F(JsonRpcServiceUnitTest, GetIsEip1559) {
  bool callback_called = false;

  // Successful path when the network is EIP1559
  SetIsEip1559Interceptor(true);
  json_rpc_service_->GetIsEip1559(
      base::BindOnce(&OnBoolResponse, &callback_called,
                     mojom::ProviderError::kSuccess, "", true));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  // Successful path when the network is not EIP1559
  callback_called = false;
  SetIsEip1559Interceptor(false);
  json_rpc_service_->GetIsEip1559(
      base::BindOnce(&OnBoolResponse, &callback_called,
                     mojom::ProviderError::kSuccess, "", false));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  SetHTTPRequestTimeoutInterceptor();
  json_rpc_service_->GetIsEip1559(base::BindOnce(
      &OnBoolResponse, &callback_called, mojom::ProviderError::kInternalError,
      l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR), false));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  SetInvalidJsonInterceptor();
  json_rpc_service_->GetIsEip1559(base::BindOnce(
      &OnBoolResponse, &callback_called, mojom::ProviderError::kParsingError,
      l10n_util::GetStringUTF8(IDS_WALLET_PARSING_ERROR), false));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  SetLimitExceededJsonErrorResponse();
  json_rpc_service_->GetIsEip1559(base::BindOnce(
      &OnBoolResponse, &callback_called, mojom::ProviderError::kLimitExceeded,
      "Request exceeds defined limit", false));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);
}

TEST_F(JsonRpcServiceUnitTest, UpdateIsEip1559NotCalledForKnownChains) {
  TestJsonRpcServiceObserver observer(mojom::kMainnetChainId,
                                      mojom::CoinType::ETH, false);
  json_rpc_service_->AddObserver(observer.GetReceiver());
  EXPECT_TRUE(
      SetNetwork(brave_wallet::mojom::kMainnetChainId, mojom::CoinType::ETH));
  EXPECT_FALSE(observer.is_eip1559_changed_called());
}

TEST_F(JsonRpcServiceUnitTest, UpdateIsEip1559LocalhostChain) {
  TestJsonRpcServiceObserver observer(mojom::kLocalhostChainId,
                                      mojom::CoinType::ETH, true);
  json_rpc_service_->AddObserver(observer.GetReceiver());

  // Switching to localhost should update is_eip1559 to true when is_eip1559 is
  // true in the RPC response.
  EXPECT_FALSE(GetIsEip1559FromPrefs(mojom::kLocalhostChainId));
  SetIsEip1559Interceptor(true);
  EXPECT_TRUE(SetNetwork(mojom::kLocalhostChainId, mojom::CoinType::ETH));
  EXPECT_TRUE(observer.chain_changed_called());
  EXPECT_TRUE(observer.is_eip1559_changed_called());
  EXPECT_TRUE(GetIsEip1559FromPrefs(mojom::kLocalhostChainId));

  // Switching to localhost should update is_eip1559 to false when is_eip1559
  // is false in the RPC response.
  observer.Reset(mojom::kLocalhostChainId, false);
  SetIsEip1559Interceptor(false);
  EXPECT_TRUE(SetNetwork(mojom::kLocalhostChainId, mojom::CoinType::ETH));
  EXPECT_TRUE(observer.chain_changed_called());
  EXPECT_TRUE(observer.is_eip1559_changed_called());
  EXPECT_FALSE(GetIsEip1559FromPrefs(mojom::kLocalhostChainId));

  // Switch to localhost again without changing is_eip1559 should not trigger
  // event.
  observer.Reset(mojom::kLocalhostChainId, false);
  EXPECT_FALSE(GetIsEip1559FromPrefs(mojom::kLocalhostChainId));
  SetIsEip1559Interceptor(false);
  EXPECT_TRUE(SetNetwork(mojom::kLocalhostChainId, mojom::CoinType::ETH));
  EXPECT_TRUE(observer.chain_changed_called());
  EXPECT_FALSE(observer.is_eip1559_changed_called());
  EXPECT_FALSE(GetIsEip1559FromPrefs(mojom::kLocalhostChainId));

  // OnEip1559Changed will not be called if RPC fails.
  observer.Reset(mojom::kLocalhostChainId, false);
  SetHTTPRequestTimeoutInterceptor();
  EXPECT_TRUE(SetNetwork(mojom::kLocalhostChainId, mojom::CoinType::ETH));
  EXPECT_TRUE(observer.chain_changed_called());
  EXPECT_FALSE(observer.is_eip1559_changed_called());
  EXPECT_FALSE(GetIsEip1559FromPrefs(mojom::kLocalhostChainId));
}

TEST_F(JsonRpcServiceUnitTest, UpdateIsEip1559CustomChain) {
  std::vector<base::Value> values;
  mojom::NetworkInfo chain1("chain_id", "chain_name", {"https://url1.com"},
                            {"https://url1.com"}, {"https://url1.com"},
                            "symbol_name", "symbol", 11, mojom::CoinType::ETH,
                            mojom::NetworkInfoData::NewEthData(
                                mojom::NetworkInfoDataETH::New(false)));
  auto chain_ptr1 = chain1.Clone();
  values.push_back(brave_wallet::EthNetworkInfoToValue(chain_ptr1));

  mojom::NetworkInfo chain2(
      "chain_id2", "chain_name2", {"https://url2.com"}, {"https://url2.com"},
      {"https://url2.com"}, "symbol_name2", "symbol2", 22, mojom::CoinType::ETH,
      mojom::NetworkInfoData::NewEthData(mojom::NetworkInfoDataETH::New(true)));
  auto chain_ptr2 = chain2.Clone();
  values.push_back(brave_wallet::EthNetworkInfoToValue(chain_ptr2));
  UpdateCustomNetworks(prefs(), &values);

  // Switch to chain1 should trigger is_eip1559 being updated to true when
  // is_eip1559 is true in the RPC response.
  TestJsonRpcServiceObserver observer(chain1.chain_id, mojom::CoinType::ETH,
                                      true);
  json_rpc_service_->AddObserver(observer.GetReceiver());

  EXPECT_FALSE(GetIsEip1559FromPrefs(chain1.chain_id));
  SetIsEip1559Interceptor(true);
  EXPECT_TRUE(SetNetwork(chain1.chain_id, mojom::CoinType::ETH));
  EXPECT_TRUE(observer.chain_changed_called());
  EXPECT_TRUE(observer.is_eip1559_changed_called());
  EXPECT_TRUE(GetIsEip1559FromPrefs(chain1.chain_id));

  // Switch to chain2 should trigger is_eip1559 being updated to false when
  // is_eip1559 is false in the RPC response.
  observer.Reset(chain2.chain_id, false);
  EXPECT_TRUE(GetIsEip1559FromPrefs(chain2.chain_id));
  SetIsEip1559Interceptor(false);
  EXPECT_TRUE(SetNetwork(chain2.chain_id, mojom::CoinType::ETH));
  EXPECT_TRUE(observer.chain_changed_called());
  EXPECT_TRUE(observer.is_eip1559_changed_called());
  EXPECT_FALSE(GetIsEip1559FromPrefs(chain2.chain_id));

  // Switch to chain2 again without changing is_eip1559 should not trigger
  // event.
  observer.Reset(chain2.chain_id, false);
  EXPECT_FALSE(GetIsEip1559FromPrefs(chain2.chain_id));
  SetIsEip1559Interceptor(false);
  EXPECT_TRUE(SetNetwork(chain2.chain_id, mojom::CoinType::ETH));
  EXPECT_TRUE(observer.chain_changed_called());
  EXPECT_FALSE(observer.is_eip1559_changed_called());
  EXPECT_FALSE(GetIsEip1559FromPrefs(chain2.chain_id));

  // OnEip1559Changed will not be called if RPC fails.
  observer.Reset(chain2.chain_id, false);
  SetHTTPRequestTimeoutInterceptor();
  EXPECT_TRUE(SetNetwork(chain2.chain_id, mojom::CoinType::ETH));
  EXPECT_TRUE(observer.chain_changed_called());
  EXPECT_FALSE(observer.is_eip1559_changed_called());
  EXPECT_FALSE(GetIsEip1559FromPrefs(chain2.chain_id));
}

TEST_F(JsonRpcServiceUnitTest, GetEthAddrInvalidDomain) {
  const std::vector<std::string> invalid_domains = {"", ".eth", "-brave.eth",
                                                    "brave-.eth", "b.eth"};

  for (const auto& domain : invalid_domains) {
    bool callback_called = false;
    json_rpc_service_->EnsGetEthAddr(
        domain,
        base::BindOnce(&OnStringResponse, &callback_called,
                       mojom::ProviderError::kInvalidParams,
                       l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS),
                       ""));
    base::RunLoop().RunUntilIdle();
    EXPECT_TRUE(callback_called);

    callback_called = false;
    json_rpc_service_->UnstoppableDomainsGetEthAddr(
        domain,
        base::BindOnce(&OnStringResponse, &callback_called,
                       mojom::ProviderError::kInvalidParams,
                       l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS),
                       ""));
    base::RunLoop().RunUntilIdle();
    EXPECT_TRUE(callback_called);
  }
}

TEST_F(JsonRpcServiceUnitTest, IsValidDomain) {
  std::vector<std::string> valid_domains = {"brave.eth", "test.brave.eth",
                                            "brave-test.test-dev.eth"};
  for (const auto& domain : valid_domains)
    EXPECT_TRUE(json_rpc_service_->IsValidDomain(domain))
        << domain << " should be valid";

  std::vector<std::string> invalid_domains = {
      "",      ".eth",    "-brave.eth",      "brave-.eth",     "brave.e-th",
      "b.eth", "brave.e", "-brave.test.eth", "brave-.test.eth"};
  for (const auto& domain : invalid_domains)
    EXPECT_FALSE(json_rpc_service_->IsValidDomain(domain))
        << domain << " should be invalid";
}

TEST_F(JsonRpcServiceUnitTest, GetERC721OwnerOf) {
  bool callback_called = false;
  json_rpc_service_->GetERC721OwnerOf(
      "", "0x1", mojom::kMainnetChainId,
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kInvalidParams,
                     l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS),
                     ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  json_rpc_service_->GetERC721OwnerOf(
      "0x06012c8cf97BEaD5deAe237070F9587f8E7A266d", "", mojom::kMainnetChainId,
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kInvalidParams,
                     l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS),
                     ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  json_rpc_service_->GetERC721OwnerOf(
      "0x06012c8cf97BEaD5deAe237070F9587f8E7A266d", "0x1", "",
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kInvalidParams,
                     l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS),
                     ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  SetInterceptor(
      "eth_call", "",
      "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":"
      "\"0x000000000000000000000000983110309620d911731ac0932219af0609"
      "1b6744\"}");

  callback_called = false;
  json_rpc_service_->GetERC721OwnerOf(
      "0x06012c8cf97BEaD5deAe237070F9587f8E7A266d", "0x1",
      mojom::kMainnetChainId,
      base::BindOnce(
          &OnStringResponse, &callback_called, mojom::ProviderError::kSuccess,
          "",
          "0x983110309620D911731Ac0932219af06091b6744"));  // checksum address
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  SetHTTPRequestTimeoutInterceptor();
  json_rpc_service_->GetERC721OwnerOf(
      "0x06012c8cf97BEaD5deAe237070F9587f8E7A266d", "0x1",
      mojom::kMainnetChainId,
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kInternalError,
                     l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR), ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  SetInvalidJsonInterceptor();
  json_rpc_service_->GetERC721OwnerOf(
      "0x06012c8cf97BEaD5deAe237070F9587f8E7A266d", "0x1",
      mojom::kMainnetChainId,
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kParsingError,
                     l10n_util::GetStringUTF8(IDS_WALLET_PARSING_ERROR), ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  SetLimitExceededJsonErrorResponse();
  json_rpc_service_->GetERC721OwnerOf(
      "0x06012c8cf97BEaD5deAe237070F9587f8E7A266d", "0x1",
      mojom::kMainnetChainId,
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kLimitExceeded,
                     "Request exceeds defined limit", ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);
}

TEST_F(JsonRpcServiceUnitTest, GetERC721Metadata) {
  bool callback_called = false;
  std::string https_token_uri_response =
      "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":"
      "\"0x00000000000000000000000000000000000000000000000000000000"
      "000000200000000000000000000000000000000000000000000000000000"
      "00000000002468747470733a2f2f696e76697369626c65667269656e6473"
      "2e696f2f6170692f31383137000000000000000000000000000000000000"
      "00000000000000000000"
      "\"}";
  std::string data_token_uri_response;
  std::string interface_supported_response =
      "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":"
      "\"0x00000000000000000000000000000000000000000000000000000000"
      "00000001"
      "\"}";
  std::string exceeds_limit_json =
      R"({ "jsonrpc":"2.0", "id":1, "error": { "code":-32005, "message": "Request exceeds defined limit" } })";
  std::string interface_not_supported_response =
      "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":"
      "\"0x00000000000000000000000000000000000000000000000000000000"
      "00000000"
      "\"}";
  std::string invalid_json =
      "It might make sense just to get some in case it catches on";
  std::string https_metadata_response =
      R"({"attributes":[{"trait_type":"Feet","value":"Green Shoes"},{"trait_type":"Legs","value":"Tan Pants"},{"trait_type":"Suspenders","value":"White Suspenders"},{"trait_type":"Upper Body","value":"Indigo Turtleneck"},{"trait_type":"Sleeves","value":"Long Sleeves"},{"trait_type":"Hat","value":"Yellow / Blue Pointy Beanie"},{"trait_type":"Eyes","value":"White Nerd Glasses"},{"trait_type":"Mouth","value":"Toothpick"},{"trait_type":"Ears","value":"Bing Bong Stick"},{"trait_type":"Right Arm","value":"Swinging"},{"trait_type":"Left Arm","value":"Diamond Hand"},{"trait_type":"Background","value":"Blue"}],"description":"5,000 animated Invisible Friends hiding in the metaverse. A collection by Markus Magnusson & Random Character Collective.","image":"https://rcc.mypinata.cloud/ipfs/QmXmuSenZRnofhGMz2NyT3Yc4Zrty1TypuiBKDcaBsNw9V/1817.gif","name":"Invisible Friends #1817"})";
  std::string ipfs_token_uri_response =
      "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":"
      "\"0x00000000000000000000000000000000000000000000000000000000000000200000"
      "00000000000000000000000000000000000000000000000000000000003a697066733a2f"
      "2f516d65536a53696e4870506e6d586d73704d6a776958794e367a533445397a63636172"
      "694752336a7863615774712f31383137000000000000"
      "\"}";

  std::string ipfs_metadata_response =
      R"({"attributes":[{"trait_type":"Mouth","value":"Bored Cigarette"},{"trait_type":"Fur","value":"Gray"},{"trait_type":"Background","value":"Aquamarine"},{"trait_type":"Clothes","value":"Tuxedo Tee"},{"trait_type":"Hat","value":"Bayc Hat Black"},{"trait_type":"Eyes","value":"Coins"}],"image":"ipfs://QmQ82uDT3JyUMsoZuaFBYuEucF654CYE5ktPUrnA5d4VDH"})";
  // Invalid inputs
  // Invalid contract address (1/3)
  json_rpc_service_->GetERC721Metadata(
      "", "0x1", mojom::kMainnetChainId,
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kInvalidParams,
                     l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS),
                     ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  // Invalid token ID (2/3)
  callback_called = false;
  json_rpc_service_->GetERC721Metadata(
      "0x06012c8cf97BEaD5deAe237070F9587f8E7A266d", "", mojom::kMainnetChainId,
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kInvalidParams,
                     l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS),

                     ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  // Invalid chain ID (3/3)
  callback_called = false;
  json_rpc_service_->GetERC721Metadata(
      "0x06012c8cf97BEaD5deAe237070F9587f8E7A266d", "0x1", "",
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kInvalidParams,
                     l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS),
                     ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  // Valid inputs
  // HTTP URI (1/3)
  callback_called = false;

  VLOG(0) << "In the test, https_metadata_response is "
          << https_metadata_response;
  SetERC721MetadataInterceptor(interface_supported_response,
                               https_token_uri_response,
                               https_metadata_response);
  json_rpc_service_->GetERC721Metadata(
      "0x59468516a8259058bad1ca5f8f4bff190d30e066", "0x719",
      mojom::kMainnetChainId,
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kSuccess, "",
                     https_metadata_response));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  // IPFS URI (2/3)
  callback_called = false;
  SetERC721MetadataInterceptor(interface_supported_response,
                               ipfs_token_uri_response, ipfs_metadata_response);
  json_rpc_service_->GetERC721Metadata(
      "0xbc4ca0eda7647a8ab7c2061c2e118a18a936f13d", "0x719",
      mojom::kMainnetChainId,
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kSuccess, "",
                     ipfs_metadata_response));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  // Data URI (3/3)
  callback_called = false;
  data_token_uri_response =
      "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":"
      "\"0x00000000000000000000000000000000000000000000000000000000000000200000"
      "000000000000000000000000000000000000000000000000000000003725646174613a61"
      "70706c69636174696f6e2f6a736f6e3b6261736536342c65794a755957316c496a6f6956"
      "5735706333646863434174494441754d7955674c534256546b6b76563056555343417449"
      "444d354c6a4d794d7a772b4d5455334c6a493049697767496d526c63324e796158423061"
      "573975496a6f69564768706379424f526c5167636d5677636d567a5a5735306379426849"
      "477870635856705a476c306553427762334e7064476c7662694270626942684946567561"
      "584e3359584167566a4d675655354a4c5664465645676763473976624334675647686c49"
      "473933626d56794947396d4948526f61584d67546b5a5549474e6862694274623252705a"
      "6e6b6762334967636d566b5a5756744948526f5a53427762334e7064476c766269356362"
      "6c787555473976624342425a4752795a584e7a4f6941776544466b4e4449774e6a526d59"
      "7a52695a5749315a6a6868595759344e5759304e6a453359575534596a4e694e57493459"
      "6d51344d444663626c564f535342425a4752795a584e7a4f6941776544466d4f5467304d"
      "4745344e57513159575931596d59785a4445334e6a4a6d4f544931596d52685a47526a4e"
      "4449774d5759354f445263626c6446564567675157526b636d567a637a6f674d48686a4d"
      "444a685957457a4f5749794d6a4e6d5a54686b4d4745775a54566a4e4759794e3256685a"
      "446b774f444e6a4e7a553259324d79584735475a57556756476c6c636a6f674d43347a4a"
      "567875564739725a5734675355513649444663626c787534707167373769504945524a55"
      "304e4d51556c4e52564936494552315a53426b615778705a3256755932556761584d6761"
      "5731775a584a6864476c325a534233614756754947467a6332567a63326c755a79423061"
      "476c7a4945354756433467545746725a53427a64584a6c49485276613256754947466b5a"
      "484a6c63334e6c637942745958526a61434230614755675a5868775a574e305a57516764"
      "4739725a57357a4c434268637942306232746c6269427a655731696232787a4947316865"
      "5342695a53427062576c305958526c5a4334694c434169615731685a3255694f6941695a"
      "474630595470706257466e5a53397a646d6372654731734f324a68633255324e43785153"
      "453479576e6c434d324658556a4268524442705457707264306c70516d39615632787559"
      "5568524f556c715658644e51306c6e5a47317362475177536e5a6c5244427054554e4264"
      "306c455354564e513045785455524261556c4961485269527a563655464e4b6232524955"
      "6e6450615468325a444e6b4d3078755933704d62546c35576e6b34655531455158644d4d"
      "303479576e6c4a5a3256484d584e69626b30325a55643463474a74637a6c4b4d6d67775a"
      "4568424e6b78354f544e6b4d324e315a48704e6457497a536d354d656b55315431527264"
      "6d56486548426962584e755547703461317058576e7051616e6874595664344d46705953"
      "576468563145355357315a65456c714e44686162565a4b596c6447626c7054516e6c6157"
      "453478596b68524f556c755158644a61554930596b6473645746366347396a62565a7455"
      "464e4b61316c59556d6850625778305756646b6245777a546a4a6165585130596c64334e"
      "316c74526e706156466b7754455a4353565271536d466c56556c3657565a6b5530314852"
      "6b564e527a564f595731304d314e7562454e694d584259596b633161464e475254565462"
      "6e42575a44417852466b795a4774695633687a576b524353325274566b564e527a564f55"
      "5442474d314e56556b704f5654464555565247546c4a46526e5654565768765a45644b53"
      "453559634646564d6c4a32576b566f553251774f5842505346707254544a52656c52484e"
      "57706c6133683054316873595756556144565556564a435a444233656c5271536d466c56"
      "3031795655566f53324a47613370565632527254573134636c70465a4735505658413255"
      "315257546c4e4653544254626d7844596a467757474a484e576854526b55315532357756"
      "6d51774d556c52616c4a4c5a56564b64466c575a44526a4d554a5557544a77546c597861"
      "7a46554d464a535a4442774e55394464464652656d7732576b6378616b743553585a5161"
      "6e6874576c567364466c585a47784a53457073597a4e57633252454d476c6a5245567053"
      "55686f633246584e584a5062576835576c645a4f556c74556d686b523055325956637861"
      "466f7956585a6a4d317075537a4e6f64474a4564476c5a57453573546d70526331564661"
      "45394e624841315557704f614659785358645a56564633596d73786357457a5a45746c56"
      "5570325632786b63324a74526b6c565647784c5a5778574d315256546d70614d6c4a3059"
      "6b64346130314663444a6156564633596d7378524646595a457053525773785646564f51"
      "6b31564d555652567a564b5530646f4d466c72597a466c62454a55576b633561314e4753"
      "6a4e554d6d73305a473152656c7045546b316962553432564563774e5756576344565053"
      "47784f556b56474d315245546b394e6248413157586c30555649774e58645a4d6a465059"
      "7a4677564646746347785352454a3156465a53616d4a7262456855616c5a5256544a4f4e"
      "565275634670696132784a5531527353325672566a565556576844546b56774e5646744d"
      "5768574d32683656555a4f616d467362445a5257477861566a426162314e75617a524c4d"
      "554a455431687761324a5854584a4a615467725545646162464e584d5768614d6c566e59"
      "323157656d5258654442515530703354576c4a5a3256486548426962584d325955684b62"
      "4670714d476c61523059775756527763474a58526d3561557a6c365a47316a636d56484d"
      "584e504d6b706f597a4a564d6b354465464654525452355632357351303079526c685661"
      "6b4a6f556b52436456525863484a6b4d48413155573035595659796548565a5657685354"
      "3156774e6c5a595a4535524d6b3575576b637863324a4855586454626c7073556b524364"
      "565256546b4a6b4d47784655315257546c45775258685556564a43596d74735357464955"
      "6d6c53656c593256555a4f61324979556b6c56626d52515956526f4d6c7045546d744e4d"
      "48683157544e7754574a5562445658626d73305a565578525646595a45314e4d44523556"
      "323573616b7378516b6855626b4a71596c5531656c6473546b4e6862565a465455633154"
      "6d46724d444254626d784459573157565531484e55355752565931553235735132565751"
      "6c525a4d32684f5957744b4d317056546d70614d584230596b684f61564a45516e565457"
      "4842435a555a7763574636556b39524d6b3479565564774d325274545870586254565257"
      "6e6f774f556c7051585a51616e6874576c567364466c585a47784a53457073597a4e5763"
      "3252454d476c6a524531705355686f633246584e584a5062576835576c645a4f556c7455"
      "6d686b523055325956637861466f7956585a6a4d317075537a4e6f64474a4564476c5a57"
      "453573546d7052633156466145394e624841315557704f614659785358645a5656463359"
      "6d73786357457a5a45746c565570325632786b63324a74526b6c565647784c5a5778574d"
      "315256546d70614d6c4a30596b64346130314663444a6156564633596d7378524646595a"
      "457053525773785646564f516b31564d555652567a564b5530646f4d466c72597a466c62"
      "454a55576b633561314e47536a4e554d6d73305a473152656c7045546b31696255343256"
      "4563774e575657634456505347784f556b56474d315245546b394e6248413157586c3055"
      "5649774e58645a4d6a4650597a4677564646746347785352454a3156466477516b307763"
      "44565262584273566b524364565272556c5a6c565841315557357355565579546a525556"
      "564a445a444a5752466b795a474669563368365757745264324a7262445a5a656b5a5059"
      "6c5531635652586247706b62454a785a444e61616b30786348565652324d3555464e4a5a"
      "3078364e44686162565a44596b645764567044516e52694d6c4a7355464e4b646d527456"
      "6e6c695230593153576c4363474a714d476c6a5245467053556473645531714d476c6a52"
      "45567053554d344b314248576d785262586873596d31525a324a584f5774615644427057"
      "6c686f616d4a49566e7068567a6c3153576c4363474a7153546c4a626b463553576c4264"
      "6c4271654731615655707a576c633161306c484d585a6152315535535730354d6c705953"
      "6e4e5a5747747053556473645531714d476c6a524531705355684b62474d7a566e4e6b52"
      "4442705757313462474a74556c426b5746467053554d344b314248576d78534d6b597859"
      "7a4e4f63466c584e554e6953465a355355647364564254536d6c6952315a31576b55354d"
      "5752445357646a4d314a72556b64574d6d4658526a4268567a6c3155464e4a4d45317053"
      "57644d656a513454444a6163474a49556d786a616a526e5545644f63324659516c465a57"
      "464a765355647361314254536d70694d307031576c684b656b6c714e44686a62565a715a"
      "454e434d324658556a4268524442705457707264306c70516d396156327875595568524f"
      "556c715658644e51306c6e5932356e4f556c7155586c4a61554a355a5651776155354553"
      "576c4a517a677255454d35616d4a486248645652305977595551304f474e48526a426851"
      "304a77576b517761575248566a526b517a4633575668536230785852576c4a5231453553"
      "5773774d4531445158684e61554a4a5457705664306c4652586c505130463554304e4264"
      "306c455157644e55304635546e706e5a30354551576457616c457954554e43516b31715a"
      "32644e616d646e54554e4264306c455257644e616c5633535552524e453944516b6c4f52"
      "45466e5556524a4e456c455354524a5245466e54554e4265456c4552586c4a5246457954"
      "554e43563035455157645256456b305355524a4e456c455157644e513046345355525264"
      "306c4552586c4a5347397053554d344b314249516d686b5232646e595664524f556c744d"
      "584269625778305756684261556c4855546c4a617a4235545870525a3035455554425265"
      "6b6c36546b4e424d45355559335650564645315355524a4d4531704e486c4e5530457754"
      "6d704e5a3031715658704a5246457954586c4a5a3078364e4468616257787a5a45645765"
      "556c486247745155306f77596a4e4264474e74566d3568567a6c315446644b6332525953"
      "576c51616e6874576c566b61475259546e706856305a31555731344d574e70516e426961"
      "6a4270565449354d574e74546d78534d30706f5930646f63466c355357646a4d314a7255"
      "6b64574d6d4658526a4268567a6c3155464e4a655535445357644d656a513454444a6163"
      "474a49556d786a616a5134596b647364567058526e6c534d30706f576b647362474a7555"
      "576468563145355357316b65566c585558526b574546705355686e654642545358684a61"
      "55493054576f77615531445357646c564555355357704661556c4961336c5155306c3353"
      "576f304f474d7a556e5a6a51304a32576d3161656c705955546c4a616b463154554e4a5a"
      "324d7a556e5a6a517a4671596a4a34646d4e714d476c6b4d6d68775a45645661556c4954"
      "6a42694d304630596a4e4361466b796244426c5644427054564e4a5a3078364e44686a4d"
      "314a3259304e43646c7074576e70615746453553576b304e556c70516e706b527a6c3354"
      "46644f646d4a484f586c5155306f7a595564734d4670545357646a4d314a3259304d7864"
      "6d4e48526d70685746493155464e4a64306c7051585a51616e6432596b64736456705852"
      "6e6c534d30706f576b647362474a755553745152336877596d315761474e725a486c5a56"
      "314a77576c63314d456c4862477451553070755932314761307858556e5a6b4d6a527053"
      "55686e654642545358644a6155493054576f77615531545357646c564555355357704261"
      "556c4961336c5155306c3453576f304f474d7a556e5a6a51304a32576d3161656c705955"
      "546c4a616b463154554e4a5a324d7a556e5a6a517a4671596a4a34646d4e714d476c6b4d"
      "6d68775a45645661556c49546a42694d304630596a4e4361466b796244426c5644427054"
      "564e4a5a3078364e44686a4d314a3259304e43646c7074576e7061574645355357704264"
      "5539545357646a4d314a3259304d78616d497965485a6a616a42705a444a6f6347524856"
      "576c4a53453477596a4e426447497a516d685a4d6d77775a565177615531445357644d65"
      "6a513454444a3463474a74566d686a6132523557566453634670584e544251616e683057"
      "56684f636b6c4862477451553070745756645362457859566e644a61554a305756684f63"
      "6c45794f58566b52315a315a455a5764574659556e7051553070325757317762466b7a55"
      "6b4e694d315a31576b647364566f77536e5a6c51306b725545684b62466b7a5557646b4d"
      "6d78725a45646e4f556c7152576c4a523268735956646b623252454d476c4e55306c6e57"
      "6d317363324a454d476c6b5745707a53304e4f626d4e74526d744d57465a3353314e4a5a"
      "3078364e44684d4d6a466f597a4a7a4b3142484d57686a4d6e4e6e595664524f556c7457"
      "6d686152315630576b63354d324a705357646956305a365954424f646d4a75556d786962"
      "6c4a57596d31734d474e364d476c694d6b7078576c644f4d4646744f54466962564a7759"
      "6d316b5132497a5a326c51616e6835576c644f4d456c495a48426153464a7655464e4a65"
      "456c70516d396156327875595568524f556c7152576c4a52317077596b64334f556c7556"
      "6e6c6951326471576a4e4b614670444d5774694d32523153314e4a5a3078364e44684d4d"
      "6a466f597a4a7a4b3142484d57686a4d6e4e6e595664524f556c744e585a696256567053"
      "55637861474d79644552694d6a5577576c63314d465a584e58426b534530355357303561"
      "574674566d706b525570325a466331613246584e57355262546b3053576f304f474e7456"
      "6d706b5130497a595664534d4746454d476c4e55306c6e5955645763466f796144425155"
      "306c3453576c436257465865484e5155306f7a595564734d4670545357644d656a513454"
      "44497861474d796379745152336877596d315761474e725a486c5a56314a77576c63314d"
      "456c4862477451553070755932314761307859546a566956307032596b4e4a4b31424954"
      "6a42694d30466e596a4a6162574d79566a425155306c335447706a61556c49546a42694d"
      "304630575449356332497a53546c4a626d52765956685362456c70516e706b527a6c3354"
      "46633564316c58546e426b534773355357704661556c444f43745153453477596a4e425a"
      "324979576d316a4d6c597755464e4a6455395556576c4a53453477596a4e4264466b794f"
      "584e694d306b355357356b62324659556d784a61554a365a456335643078584f58645a56"
      "3035775a4568724f556c7151576c4a517a677255454d35633246584e57785a5745704959"
      "32314761324658566e566b52445134596c6447656d4635516e426152444270576d314761"
      "3170544d58706c567a4670596a4a3361556c484d57686a4d6e5245596a49314d4670584e"
      "544257567a56775a45684e4f556c75566e70615745705559306447616c70564f58565757"
      "45357353576f304f474e74566d706b5130497a595664534d4746454d476c4e616d743359"
      "30686e61556c4861477868563252765a455177615531715158646a534764705355646163"
      "474a48647a6c4a626c5a35596b4e6e616c6f7a536d6861517a46365a5663786157497964"
      "33424a6155463255477033646d4a58526e7068656a513454444a53624670755453745152"
      "324e6e57544a3463474e444d58645a57464a7655464e4b4d574e746432394a4d6b353259"
      "32303162474e755458424a616a513459323157616d5244516d31685633687a55464e4a65"
      "467071617a524f524546705355686e4f556c71516e646c51306c6e5a5651776155314951"
      "6a524a6155497a595664534d4746454d476c4e616d74335930686e61556c486147786856"
      "3252765a455177615535555158646a5347647053554d344b314249536d785a4d31466e59"
      "7a4e534e574a4856546c4a62567077596b685362474e716232646b5745707a53304e4f62"
      "55315461326c4a5347633553577043643256445357646c56444270545568434e456c7051"
      "6a4e685631497759555177615531716133646a534764705355646f624746585a47396b52"
      "444270546c524264324e495a326c4a517a677253555234626b6c49546a426c5633687355"
      "464e4b625746586544426157456b325a46684b63307444546a42694d3046305932315762"
      "6d46584f58564d5630707a5a46684a63453935516a426a62555a31597a4a61646d4e744d"
      "445a6a4d6b356f596b6456623031544e44464c56484e6e5a45684b61474a75546d31694d"
      "30703054466335655746585a484269616e4271576c63314d4670595357646b527a6c3354"
      "336c4a4b314249536d785a4d31466e576d317363324a454d476c6962546c31576c4e4a5a"
      "3256454d476c4e5345493053576c434e5642545358646a534764705355686b6346704955"
      "6d395155306c3554315243643256445357646852315a77576a4a6f4d4642545354464e52"
      "454a335a554e4a5a3078364e4468615633687a59566843656c7054516d706c5244427054"
      "6c524262456c70516d706c56444270545568434e456c70516e6c6c524442705456526e64"
      "324e495a326c4a53456f3155464e4a65453171516e646c51306c6e576d317363324a454d"
      "476c4a656b463354554e4a5a32497a516d685a4d6d77775a565177615531444e44524f55"
      "306c6e54486f304f457779597974515345707357544e525a3256454d476c4e51306c6e5a"
      "565177615531445357646b4d6d78725a45646e4f556c715354564e51306c6e5955645763"
      "466f796144425155306b785455524261556c49536a525155306b7754576c4a5a324e7561"
      "7a6c4a616c463553576c436257465865484e5155307035576a4a4b6145744551584e4e51"
      "3364335445524263456c70516e706b5345703259544a564f556c75536d355a6255567654"
      "5770564d5578455354464f55336435546c5256633031444e486c4c55306c6e54486f304f"
      "4577795979745153464a735a5568525a325248566a526b517a4635576c63316131705953"
      "6e426962574d355357303564325248624852685748427356544e436246705855576c5161"
      "6e6777576c686f4d465648526a426851304a365a456447655752464f573161626b35735a"
      "455177615578555258644e513156705355646163474a48647a6c4a626d52765956685362"
      "456c70516d31694d6a55775446646161474a5862484e6c56444270536a424f646d525953"
      "6e426157456c6e564731574d30703564326469567a6c31596a4e4f64316c58546d784a61"
      "554a74596a49314d457859546e426c625655355357704664324e495a326c4a5347687a59"
      "566331636b397461486c6156316b3553576c4f4d4670596144424d57454a6f5a45646e64"
      "466c545353744e534768715455524b61466c585258705056306c355457704f6256705561"
      "47744e52305633576c5257616b354857586c4f4d6c5a6f576b527264303945546d704f65"
      "6c557957544a4e65556c505330467661554a59556c5a5353556c45654768696257783057"
      "56685362456c48526d7461523277775956686162464254536e706b567a4270535564474d"
      "475249536e425a626c5977576c553161474a5856546c4a626b34775756684b4d46517957"
      "6d316a4d6c597753576c4362574e744f58525155306c33536c4e4a5a3252484f446c4a61"
      "6b563354554e5661556c48536d78614d6d783155464e4a64324e355357646153465a3555"
      "464e4a656b314954576c4a534570735930645761475246546e5a6b567a557755464e4b63"
      "474a74556d7861625778315956685362456c7051585a51616e64325a4564574e47524751"
      "6d686b52326372535552344d467059614442565230597759554e43656d5248526e6c6b52"
      "546c74576d354f624752454d476c4e513156705355646163474a48647a6c4a626d527659"
      "56685362456c70516d31694d6a55775446646161474a5862484e6c56444270536a424f64"
      "6d5259536e426157456c6e564731574d30703564326469567a6c31596a4e4f64316c5854"
      "6d784a61554a74596a49314d457859546e426c625655355357704664324e495a326c4a53"
      "47687a59566331636b397461486c6156316b3553576c4f4d4670596144424d57454a6f5a"
      "45646e64466c545353744e534768715455524b61466c585258705056306c355457704f62"
      "5670556147744e52305633576c5257616b354857586c4f4d6c5a6f576b52726430394554"
      "6d704f656c557957544a4e65556c505330467661554a59556c5a5353556c456547686962"
      "5778305756685362456c48526d7461523277775956686162464254536e706b567a427053"
      "5564474d475249536e425a626c5977576c553161474a5856546c4a626b34775756684b4d"
      "465179576d316a4d6c597753576c4362574e744f58525155306c33536c4e4a5a3252484f"
      "446c4a616b563354554e5661556c48536d78614d6d783155464e4a64324e355357646153"
      "465a3555464e4a656b314954576c4a534570735930645761475246546e5a6b567a557755"
      "464e4b63474a74556d7861625778315956685362456c7051585a516155453454444e5362"
      "475649556c465a57464a76554770344d467059614442565230597759554e43656d524852"
      "6e6c6b52546c74576d354f624752454d476c4f5645467353576c436257465865484e5155"
      "306f7a595564734d4670545357646162546c315a454d7862566c584d5842695347733553"
      "576c6b5247497a566e6c6856315a35535555316247523559334e4a527a4632596d303565"
      "6d4e48526d706155306c6e576d3035645752444d5870685748427355464e4a6545314951"
      "6a524a61554930596b6473645746366347396a62565a7455464e4a616d5248566a526b51"
      "7a4633575668536230785852576c51616b49305456645a4e5539455558645a5647637857"
      "6b525761467071566d6c61616b5a725456526a4d6b31745754564e616c5a70576b644761"
      "3170485454424e616b4634576d70724e45354452476c6e53306c6e566c5531536b6c4565"
      "476869625778305756685362456c48526d7461523277775956686162464254536e706b56"
      "7a4270535564474d475249536e425a626c5977576c553161474a5856546c4a626b347757"
      "56684b4d465179576d316a4d6c597753576c4362574e744f58525155306c33536c4e4a5a"
      "3252484f446c4a616b563354554e5661556c48536d78614d6d783155464e4a64324e3553"
      "57646153465a3555464e4a656b314954576c4a534570735930645761475246546e5a6b56"
      "7a557755464e4b63474a74556d7861625778315956685362456c7051585a51616e64325a"
      "4564574e475247516d686b523263725545685362475649556c465a57464a765355684f4d"
      "466c59536a42554d6c7074597a4a574d4642545358524f5645467353576c436257465865"
      "484e5155306f7a595564734d4670545357646162546c315a454d7862566c584d58426953"
      "47733553576c6b5247497a566e6c6856315a35535555316247523559334e4a527a463259"
      "6d3035656d4e48526d706155306c6e576d3035645752444d5870685748427355464e4a65"
      "453149516a524a61554930596b6473645746366347396a62565a7455464e4a616d524856"
      "6a526b517a4633575668536230785852576c51616b49305456645a4e5539455558645a56"
      "476378576b525761467071566d6c61616b5a725456526a4d6b31745754564e616c5a7057"
      "6b6447613170485454424e616b4634576d70724e45354452476c6e53306c6e566c553153"
      "6b6c4565476869625778305756685362456c48526d746152327777595668616246425453"
      "6e706b567a4270535564474d475249536e425a626c5977576c553161474a5856546c4a62"
      "6b34775756684b4d465179576d316a4d6c597753576c4362574e744f58525155306c3353"
      "6c4e4a5a3252484f446c4a616b563354554e5661556c48536d78614d6d783155464e4a64"
      "324e355357646153465a3555464e4a656b314954576c4a53457073593064576147524654"
      "6e5a6b567a557755464e4b63474a74556d7861625778315956685362456c7051585a5161"
      "6e64325a4564574e475247516d686b5232637255454d354d46705961444251616e687553"
      "55637861474d79637a6c4a626c5a35596b4e6e616c7074526d7461557a46365a56637861"
      "5749796433424a616a513459323157616d5244516d31685633687a55464e4b645749794e"
      "57784a6155493055464e4a64324e495a326c4a5347733553577043643256445357646b4d"
      "6d78725a45646e4f556c715354564e5345493053576c4362317058624735685346453553"
      "57704a64303149516a524a6155463255476c424f475248566a526b5130493155464e4a4d"
      "303149516a524a6155493055464e4a656b3175516a524a61554a74595664346331425453"
      "6a4e6852327777576c4e4a5a3170744f58566b517a46745756637863474a49617a6c4a61"
      "575245596a4e5765574658566e6c4a525456735a486c6a63306c484d585a6962546c3659"
      "306447616c70545357646162546c315a454d784d31705862473568534645355357704a64"
      "3031445357646162546c315a454d78656d46596347785155306c36546d35434e456c714e"
      "565a5561327432566a425756564e4564335a6b523159305a4551304f475248566a526b51"
      "30493155464e4a65453155566e646c51306c6e5a55517761553136536e646c51306c6e57"
      "6d317363324a454d476c6b4d6d68775a45645661556c48576e5a69626c4630576d314764"
      "4746586544565155306c75555449354d574e746247786a61554a50576c686a626b784451"
      "6e52694d6a5632597a4e4361466b7956576c4a52317032596d355264475179566e42614d"
      "6d677755464e4a6555314551576c4a52317032596d355264474d7962445a615644427054"
      "587061643256445353744e517a5236536c5233646d5248566a526b5244513454444a6a4b"
      "314249536d785a4d31466e5a5551776155315557576c4a53477335535770464d6b6c7051"
      "6a4e685631497759555177615531715654524a61554a76576c6473626d464955546c4a61"
      "6c457954304e4a5a324e755a7a6c4a616b6b7953576c43655756554d476c4e616c6c7053"
      "55646163474a48647a6c4a626b707557573146623031446433644d5245467a54554e7261"
      "556c49546a426a62546c79576c517761574e745a476c5a55326435546c52566330317156"
      "54464d52456b78546c4e33643078715358424a6155463255477034626b6c484d57686a4d"
      "6e4d355357355765574a445a32706162555a72576c4d786132497a5a48564c55306c6e59"
      "7a4e534e574a4856546c4a626c4a3557566331656c70744f586c69564841775932314764"
      "574d796547686b52315676546e704b6432564464336850524778335a554e726156427165"
      "486c61563034775355686e4f556c704d48684f626b493053576c434e5642545358524e56"
      "4670335a554e4a5a3251796247746b52326335535770464e453149516a524a61554a7657"
      "6c6473626d464955546c4a616b5530545568434e456c70516d31685633687a55464e4b64"
      "5749794e57784a615546325547703464316c59556d394a523145355357737765456c4552"
      "6b524e5530453054314e424d5535354e44464a52455577546c4e42654535455657644e56"
      "46457853576c43656d5249536e5a684d6c55355357354b626c6c745257394e5133643354"
      "455242633031444e48704c55306c6e597a4e53655749796447784d57475277576b685362"
      "3142545358704e626b493053576c436257465865484e5155307031596a493162456c7051"
      "6e706b5345703259544a5664474a48624856615630356f5930517761574e744f54466962"
      "56467053554d344b3142444f573551616e68755355637861474d79637a6c4a626c5a3559"
      "6b4e6e616c7074526d7461557a4672596a4e6b645574545357646a4d314931596b64564f"
      "556c75556e6c5a567a5636576d303565574a556344426a62555a31597a4a346147524856"
      "57394f656b70335a554e33654539456248646c513274705547703465567058546a424a53"
      "47633553576b7765453575516a524a6155493155464e4a64453155576e646c51306c6e5a"
      "444a73613252485a7a6c4a616b5530545568434e456c70516d396156327875595568524f"
      "556c715254524e5345493053576c436257465865484e5155307031596a493162456c7051"
      "585a51616e68335756685362306c4855546c4a617a423453555247524531545154525055"
      "304578546e6b304d556c455254424f55304634546b52565a3031555554464a61554a365a"
      "45684b646d457956546c4a626b707557573146623031715654464d52456b78546c4e3365"
      "55355556584e4e553274705355646163474a48647a6c4a62545632596d315661556c4954"
      "6a426a62546c79576c4d78633246584e57785a4d6b5a3355464e4b6557497a566e566151"
      "306c6e54486f304f45777959797451523035775932314f63317054516d706c5244427054"
      "6e704f643256445357645a4d327335535770464e553149516a524a61554a3555464e4a4d"
      "474e495a326c4a52317077596b64334f556c755a47396857464a7353576c42646c427165"
      "47706857457071596b64565a316b7a5a7a6c4a616d4e365930686e61556c48546a565155"
      "306c3454315243643256445357646a616a42705457705364325644535764616257787a59"
      "6b517761574a744f58566155306c6e597a4e53655749796447785155306f7a595564734d"
      "4670545357644d656a526e5545646a5a324d7a556a5669523155355357355365566c584e"
      "58706162546c35596c52774d474e74526e566a4d6e686f5a456456623031716248646c51"
      "33646e5458706e4d474e495a33424a616a513459323157616d5244516a4e685631497759"
      "55517761553571546e646c51306c6e5955645763466f796144425155306c35546d35434e"
      "456c70516e6c6c52444270543068434e456c70516e6c6c56444270543068434e456c7051"
      "6d31685633687a55464e4b65566f79536d684c5245467a54554e33643078455158564f61"
      "57747053554d344b314249556d786c5346466e5a55517761553155536e646c51306c6e5a"
      "565177615531555a48646c51306c6e576d3035645752444d57315a567a4677596b68724f"
      "556c705a4552694d315a355956645765556c464e57786b65574e7a53556378646d4a744f"
      "58706a52305a71576c4e4a5a3170744f58566b517a463659566877624642545358684e62"
      "6b493053576c436257465865484e5155306f7a595564734d4670545353745153464a3659"
      "30644764556c48576e4269523363355357354b626c6c745257394e616c55785445524a4d"
      "55355464336c4f5646567a54554d304d6b7454535374545656453253555233646d524954"
      "6e645a567a517254565233646d5248566a526b5244513454444a6a4b306c456547354a53"
      "4534775a56643462464254536a426a62555a31597a4a61646d4e744d445a6b5345706f59"
      "6d354f63316c59556d784c52456b315930686e63306c455558684f5345493053314e4a4b"
      "314249536d785a4d31466e5a444a73613252485a7a6c4a616b5577545568434e456c7051"
      "6d396156327875595568524f556c7153544a6a534764705355684b4e4642545354526a53"
      "4764705355684b4e5642545354526a534764705355646163474a48647a6c4a626b707557"
      "573146623031446433644d5245467a54554d304d6b74545357644d656a51345a4564574e"
      "475244516a525155306c34545735434e456c70516a565155306c34546a4e434e456c7051"
      "6d31694d6a55775446646161474a5862484e6c56444270536a424f646d5259536e426157"
      "456c6e564731574d30703564326469567a6c31596a4e4f64316c58546d784a61554a7459"
      "6a49314d457859546e426c625655355357704665574e495a326c4a52317077596b64334f"
      "556c755a47396857464a7353576f304f475249546e645a567a526e576d317363324a454d"
      "476c6a6257527057564e6e6555355556584e4e616c55785445524a4d5535546433644d61"
      "6c6c7753576f31546d46584e47645752327871595870765a3142444f54426a4d304a6f59"
      "6d6f3064453555515446505245453454444e536247564955537451517a6c7555476c424f"
      "467035516e706b5347787a576c517761575249536d6869626b3574596a4e4b6445397555"
      "6e6c5a567a5636596b64474d4670545a336c505745493054454e424d453545556e646c51"
      "3274705547703465567058546a424a53475277576b6853623142545358684f52454a335a"
      "554e4a5a324648566e42614d6d677755464e4a65553575516a524a61554a355a55517761"
      "553949516a524a61554a355a56517761553949516a524a61554a74595664346331425453"
      "6e6c614d6b706f53305242633031446433644d52454631546d6c7261556c444f43745153"
      "464a735a5568525a3256454d476c4e564570335a554e4a5a3256554d476c4e564752335a"
      "554e4a5a3170744f58566b517a46745756637863474a49617a6c4a61575245596a4e5765"
      "574658566e6c4a525456735a486c6a63306c484d585a6962546c3659306447616c705453"
      "57646162546c315a454d78656d46596347785155306c34545735434e456c70516d316856"
      "33687a55464e4b4d3246486244426155306b7255456853656d4e48526e564a5231707759"
      "6b64334f556c75536d355a62555676545770564d5578455354464f55336435546c525663"
      "3031444e444a4c55306b72564664474e456c47556e425a4d6e4d3253555233646d524954"
      "6e645a567a51725446524e4d6b353653586451517a6b77576c686f4d46427164335a6165"
      "6a5134576e6c43656d524962484e61564442705a45684b61474a75546d31694d30703054"
      "32355365566c584e58706952305977576c4e6e65553171576e646c5133646e546b524e65"
      "6d4e495a33424a616a513459323157616d5244516a4e6856314977595551776155313657"
      "6e646c51306c6e5955645763466f796144425155306c36546d35434e456c70516e6c6c52"
      "444270543068434e456c70516e6c6c56444270543068434e456c70516d31685633687a55"
      "464e4b645749794e57784a61554a365a45684b646d457956546c4a626b70755757314662"
      "3031715654464d52456b78546c4e336555355556584e4e517a523553314e4a5a3078364e"
      "44686a5230597759554e43656d5249536e5a684d6c5630596b647364567058546d686a52"
      "444270593230354d574a7455576c4a52314535535773774e456c4562455250517a523354"
      "5552426430354451586c4e61545131546b52724d456c4552544a4d616b6c33543152725a"
      "3031715a32644e616d4e6e5457706e61556c48576e42695233633553573031646d4a7456"
      "576c4a5345347759323035636c70554d476c6b4d6d68775a45645661556c444f43745152"
      "3035775932314f63317054516e706b5347787a576c517761575249536d6869626b357459"
      "6a4e4b64453975556e6c5a567a5636596b64474d467055546d744c524768335a554e335a"
      "3031555558564e616c5a335a554e335a303149516a524c55306c6e57544e6e4f556c7151"
      "6e646c51306c6e57544e724f556c71516e646c51306c6e59326f7761553549516a524a61"
      "554a745956643463314254536a4e6852327777576c4e4a646c427164335a61656a513457"
      "6e6c43656d524962484e61564442705a45684b61474a75546d31694d3070305432355365"
      "566c584e58706952305977576c4e6e65553171576e646c5133646e5458707265574e495a"
      "33424a616a513459323157616d5244516a4e68563149775955517761553136576e646c51"
      "306c6e5955645763466f796144425155306c36546d35434e456c70516e6c6c5244427054"
      "3068434e456c70516e6c6c56444270543068434e456c70516d31685633687a55464e4b64"
      "5749794e57784a61554a365a45684b646d457956546c4a626b7075575731466230317156"
      "54464d52456b78546c4e336555355556584e4e517a523553314e4a5a3078364e44686165"
      "6a5134593064474d474644516e706b5347787a576c517761575249536d6869626b357459"
      "6a4e4b64453975556e6c5a567a5636596b64474d4670545a7a4a6a5347647a546d35434e"
      "45745453576461524442705646524665556c45516b314e56456c31546d70566555317051"
      "54564d616c5579546c526e4d3152455254524a52455631546d70424d3034776433684e65"
      "54517a543052464e556c455258644d616b6c3454305247545531715358564e656d743554"
      "586c424d6c52455254424d616c4636546b52465a3031555258564e656c457a5430563365"
      "5535445158684e61336434546b4d304d4531365558684a524556355447705a4d55317153"
      "6b314e616b6c315458707265553135515868505258643454586b304d3039455254564a52"
      "4556365447706a4e4531556245314e5647646e5457704a6455313661336c4e4d48643454"
      "576b304d6b355553586c4a5245557754477052656b3545526b314e56456c6e5457705354"
      "5531555258564e656c457a54304e42654535444e44424e656c46345645525a5a30317153"
      "58564e656d743554544233654531444e486c4e5647643453555246656b7871597a524e56"
      "47784e54564d304d6b3145597a4e4a5245553056455272645535555754465052474e6e54"
      "56524a6455357156586c4e6133643353555246655652456133564f56466b785430526a5a"
      "3031555258564e656c457a54305633654578715758644f656d4e6e546d7433654531444e"
      "486c4e5647643453555246643078715358685052455a4e546d6c42654578715758644f65"
      "6d524e545652466455313655544e5051304531544770564d6b35555a7a4e555245563553"
      "55524359556c70516d31685633687a55464e4b4d3246486244426155306c6e54486f304f"
      "466c584e58426956305977576c5a5365566c584e58706162546c35596c4e436147524955"
      "6e6c6856306f785a45645754316c584d57785155306f775932314764574d79576e5a6a62"
      "544270535568534e574e4856546c4a626b70325a4564474d46705453576461626b703259"
      "6c517761553144515868505130463454304e4a5a3252484f446c4a616b307954554e4265"
      "4539445158685051306c6e576b6857655642545358684e534531705355684b62474e4856"
      "6d686b525535325a4663314d464254536e426962564a73576d317364574659556d784a61"
      "54677255454d35626c427164335a61656a513454444e4f4d6c70364e44306966513d3d00"
      "0000000000000000000000000000000000000000000000000000"
      "\"}";
  SetERC721MetadataInterceptor(interface_supported_response,
                               data_token_uri_response);
  json_rpc_service_->GetERC721Metadata(
      "0xbc4ca0eda7647a8ab7c2061c2e118a18a936f13d", "0x719",
      mojom::kMainnetChainId,
      base::BindOnce(
          &OnStringResponse, &callback_called, mojom::ProviderError::kSuccess,
          "",
          "{\"name\":\"Uniswap - 0.3% - UNI/WETH - 39.323<>157.24\", "
          "\"description\":\"This NFT represents a liquidity position in a "
          "Uniswap V3 UNI-WETH pool. The owner of this NFT can modify or "
          "redeem the position.\\n\\nPool Address: "
          "0x1d42064fc4beb5f8aaf85f4617ae8b3b5b8bd801\\nUNI Address: "
          "0x1f9840a85d5af5bf1d1762f925bdaddc4201f984\\nWETH Address: "
          "0xc02aaa39b223fe8d0a0e5c4f27ead9083c756cc2\\nFee Tier: 0.3%\\nToken "
          "ID: 1\\n\\n\xE2\x9A\xA0\xEF\xB8\x8F DISCLAIMER: Due diligence is "
          "imperative when assessing this NFT. Make sure token addresses match "
          "the expected tokens, as token symbols may be imitated.\", "
          "\"image\": "
          "\"data:image/"
          "svg+xml;base64,"
          "PHN2ZyB3aWR0aD0iMjkwIiBoZWlnaHQ9IjUwMCIgdmlld0JveD0iMCAwIDI5MCA1MDAi"
          "IHhtbG5zPSJodHRwOi8vd3d3LnczLm9yZy8yMDAwL3N2ZyIgeG1sbnM6eGxpbms9J2h0"
          "dHA6Ly93d3cudzMub3JnLzE5OTkveGxpbmsnPjxkZWZzPjxmaWx0ZXIgaWQ9ImYxIj48"
          "ZmVJbWFnZSByZXN1bHQ9InAwIiB4bGluazpocmVmPSJkYXRhOmltYWdlL3N2Zyt4bWw7"
          "YmFzZTY0LFBITjJaeUIzYVdSMGFEMG5Namt3SnlCb1pXbG5hSFE5SnpVd01DY2dkbWxs"
          "ZDBKdmVEMG5NQ0F3SURJNU1DQTFNREFuSUhodGJHNXpQU2RvZEhSd09pOHZkM2QzTG5j"
          "ekxtOXlaeTh5TURBd0wzTjJaeWMrUEhKbFkzUWdkMmxrZEdnOUp6STVNSEI0SnlCb1pX"
          "bG5hSFE5SnpVd01IQjRKeUJtYVd4c1BTY2pNV1k1T0RRd0p5OCtQQzl6ZG1jKyIvPjxm"
          "ZUltYWdlIHJlc3VsdD0icDEiIHhsaW5rOmhyZWY9ImRhdGE6aW1hZ2Uvc3ZnK3htbDti"
          "YXNlNjQsUEhOMlp5QjNhV1IwYUQwbk1qa3dKeUJvWldsbmFIUTlKelV3TUNjZ2RtbGxk"
          "MEp2ZUQwbk1DQXdJREk1TUNBMU1EQW5JSGh0Ykc1elBTZG9kSFJ3T2k4dmQzZDNMbmN6"
          "TG05eVp5OHlNREF3TDNOMlp5YytQR05wY21Oc1pTQmplRDBuTVRjbklHTjVQU2N5TnpZ"
          "bklISTlKekV5TUhCNEp5Qm1hV3hzUFNjall6QXlZV0ZoSnk4K1BDOXpkbWMrIi8+"
          "PGZlSW1hZ2UgcmVzdWx0PSJwMiIgeGxpbms6aHJlZj0iZGF0YTppbWFnZS9zdmcreG1s"
          "O2Jhc2U2NCxQSE4yWnlCM2FXUjBhRDBuTWprd0p5Qm9aV2xuYUhROUp6VXdNQ2NnZG1s"
          "bGQwSnZlRDBuTUNBd0lESTVNQ0ExTURBbklIaHRiRzV6UFNkb2RIUndPaTh2ZDNkM0xu"
          "Y3pMbTl5Wnk4eU1EQXdMM04yWnljK1BHTnBjbU5zWlNCamVEMG5Nak00SnlCamVUMG5N"
          "VEV5SnlCeVBTY3hNakJ3ZUNjZ1ptbHNiRDBuSXpBeFpqazROQ2N2UGp3dmMzWm5QZz09"
          "IiAvPjxmZUltYWdlIHJlc3VsdD0icDMiIHhsaW5rOmhyZWY9ImRhdGE6aW1hZ2Uvc3Zn"
          "K3htbDtiYXNlNjQsUEhOMlp5QjNhV1IwYUQwbk1qa3dKeUJvWldsbmFIUTlKelV3TUNj"
          "Z2RtbGxkMEp2ZUQwbk1DQXdJREk1TUNBMU1EQW5JSGh0Ykc1elBTZG9kSFJ3T2k4dmQz"
          "ZDNMbmN6TG05eVp5OHlNREF3TDNOMlp5YytQR05wY21Oc1pTQmplRDBuTWpBM0p5Qmpl"
          "VDBuTkRVeUp5QnlQU2N4TURCd2VDY2dabWxzYkQwbkl6YzFObU5qTWljdlBqd3ZjM1pu"
          "UGc9PSIgLz48ZmVCbGVuZCBtb2RlPSJvdmVybGF5IiBpbj0icDAiIGluMj0icDEiIC8+"
          "PGZlQmxlbmQgbW9kZT0iZXhjbHVzaW9uIiBpbjI9InAyIiAvPjxmZUJsZW5kIG1vZGU9"
          "Im92ZXJsYXkiIGluMj0icDMiIHJlc3VsdD0iYmxlbmRPdXQiIC8+"
          "PGZlR2F1c3NpYW5CbHVyIGluPSJibGVuZE91dCIgc3RkRGV2aWF0aW9uPSI0MiIgLz48"
          "L2ZpbHRlcj4gPGNsaXBQYXRoIGlkPSJjb3JuZXJzIj48cmVjdCB3aWR0aD0iMjkwIiBo"
          "ZWlnaHQ9IjUwMCIgcng9IjQyIiByeT0iNDIiIC8+"
          "PC9jbGlwUGF0aD48cGF0aCBpZD0idGV4dC1wYXRoLWEiIGQ9Ik00MCAxMiBIMjUwIEEy"
          "OCAyOCAwIDAgMSAyNzggNDAgVjQ2MCBBMjggMjggMCAwIDEgMjUwIDQ4OCBINDAgQTI4"
          "IDI4IDAgMCAxIDEyIDQ2MCBWNDAgQTI4IDI4IDAgMCAxIDQwIDEyIHoiIC8+"
          "PHBhdGggaWQ9Im1pbmltYXAiIGQ9Ik0yMzQgNDQ0QzIzNCA0NTcuOTQ5IDI0Mi4yMSA0"
          "NjMgMjUzIDQ2MyIgLz48ZmlsdGVyIGlkPSJ0b3AtcmVnaW9uLWJsdXIiPjxmZUdhdXNz"
          "aWFuQmx1ciBpbj0iU291cmNlR3JhcGhpYyIgc3RkRGV2aWF0aW9uPSIyNCIgLz48L2Zp"
          "bHRlcj48bGluZWFyR3JhZGllbnQgaWQ9ImdyYWQtdXAiIHgxPSIxIiB4Mj0iMCIgeTE9"
          "IjEiIHkyPSIwIj48c3RvcCBvZmZzZXQ9IjAuMCIgc3RvcC1jb2xvcj0id2hpdGUiIHN0"
          "b3Atb3BhY2l0eT0iMSIgLz48c3RvcCBvZmZzZXQ9Ii45IiBzdG9wLWNvbG9yPSJ3aGl0"
          "ZSIgc3RvcC1vcGFjaXR5PSIwIiAvPjwvbGluZWFyR3JhZGllbnQ+"
          "PGxpbmVhckdyYWRpZW50IGlkPSJncmFkLWRvd24iIHgxPSIwIiB4Mj0iMSIgeTE9IjAi"
          "IHkyPSIxIj48c3RvcCBvZmZzZXQ9IjAuMCIgc3RvcC1jb2xvcj0id2hpdGUiIHN0b3At"
          "b3BhY2l0eT0iMSIgLz48c3RvcCBvZmZzZXQ9IjAuOSIgc3RvcC1jb2xvcj0id2hpdGUi"
          "IHN0b3Atb3BhY2l0eT0iMCIgLz48L2xpbmVhckdyYWRpZW50PjxtYXNrIGlkPSJmYWRl"
          "LXVwIiBtYXNrQ29udGVudFVuaXRzPSJvYmplY3RCb3VuZGluZ0JveCI+"
          "PHJlY3Qgd2lkdGg9IjEiIGhlaWdodD0iMSIgZmlsbD0idXJsKCNncmFkLXVwKSIgLz48"
          "L21hc2s+"
          "PG1hc2sgaWQ9ImZhZGUtZG93biIgbWFza0NvbnRlbnRVbml0cz0ib2JqZWN0Qm91bmRp"
          "bmdCb3giPjxyZWN0IHdpZHRoPSIxIiBoZWlnaHQ9IjEiIGZpbGw9InVybCgjZ3JhZC1k"
          "b3duKSIgLz48L21hc2s+"
          "PG1hc2sgaWQ9Im5vbmUiIG1hc2tDb250ZW50VW5pdHM9Im9iamVjdEJvdW5kaW5nQm94"
          "Ij48cmVjdCB3aWR0aD0iMSIgaGVpZ2h0PSIxIiBmaWxsPSJ3aGl0ZSIgLz48L21hc2s+"
          "PGxpbmVhckdyYWRpZW50IGlkPSJncmFkLXN5bWJvbCI+"
          "PHN0b3Agb2Zmc2V0PSIwLjciIHN0b3AtY29sb3I9IndoaXRlIiBzdG9wLW9wYWNpdHk9"
          "IjEiIC8+"
          "PHN0b3Agb2Zmc2V0PSIuOTUiIHN0b3AtY29sb3I9IndoaXRlIiBzdG9wLW9wYWNpdHk9"
          "IjAiIC8+"
          "PC9saW5lYXJHcmFkaWVudD48bWFzayBpZD0iZmFkZS1zeW1ib2wiIG1hc2tDb250ZW50"
          "VW5pdHM9InVzZXJTcGFjZU9uVXNlIj48cmVjdCB3aWR0aD0iMjkwcHgiIGhlaWdodD0i"
          "MjAwcHgiIGZpbGw9InVybCgjZ3JhZC1zeW1ib2wpIiAvPjwvbWFzaz48L2RlZnM+"
          "PGcgY2xpcC1wYXRoPSJ1cmwoI2Nvcm5lcnMpIj48cmVjdCBmaWxsPSIxZjk4NDAiIHg9"
          "IjBweCIgeT0iMHB4IiB3aWR0aD0iMjkwcHgiIGhlaWdodD0iNTAwcHgiIC8+"
          "PHJlY3Qgc3R5bGU9ImZpbHRlcjogdXJsKCNmMSkiIHg9IjBweCIgeT0iMHB4IiB3aWR0"
          "aD0iMjkwcHgiIGhlaWdodD0iNTAwcHgiIC8+"
          "IDxnIHN0eWxlPSJmaWx0ZXI6dXJsKCN0b3AtcmVnaW9uLWJsdXIpOyB0cmFuc2Zvcm06"
          "c2NhbGUoMS41KTsgdHJhbnNmb3JtLW9yaWdpbjpjZW50ZXIgdG9wOyI+"
          "PHJlY3QgZmlsbD0ibm9uZSIgeD0iMHB4IiB5PSIwcHgiIHdpZHRoPSIyOTBweCIgaGVp"
          "Z2h0PSI1MDBweCIgLz48ZWxsaXBzZSBjeD0iNTAlIiBjeT0iMHB4IiByeD0iMTgwcHgi"
          "IHJ5PSIxMjBweCIgZmlsbD0iIzAwMCIgb3BhY2l0eT0iMC44NSIgLz48L2c+"
          "PHJlY3QgeD0iMCIgeT0iMCIgd2lkdGg9IjI5MCIgaGVpZ2h0PSI1MDAiIHJ4PSI0MiIg"
          "cnk9IjQyIiBmaWxsPSJyZ2JhKDAsMCwwLDApIiBzdHJva2U9InJnYmEoMjU1LDI1NSwy"
          "NTUsMC4yKSIgLz48L2c+"
          "PHRleHQgdGV4dC1yZW5kZXJpbmc9Im9wdGltaXplU3BlZWQiPjx0ZXh0UGF0aCBzdGFy"
          "dE9mZnNldD0iLTEwMCUiIGZpbGw9IndoaXRlIiBmb250LWZhbWlseT0iJ0NvdXJpZXIg"
          "TmV3JywgbW9ub3NwYWNlIiBmb250LXNpemU9IjEwcHgiIHhsaW5rOmhyZWY9IiN0ZXh0"
          "LXBhdGgtYSI+"
          "MHhjMDJhYWEzOWIyMjNmZThkMGEwZTVjNGYyN2VhZDkwODNjNzU2Y2MyIOKAoiBXRVRI"
          "IDxhbmltYXRlIGFkZGl0aXZlPSJzdW0iIGF0dHJpYnV0ZU5hbWU9InN0YXJ0T2Zmc2V0"
          "IiBmcm9tPSIwJSIgdG89IjEwMCUiIGJlZ2luPSIwcyIgZHVyPSIzMHMiIHJlcGVhdENv"
          "dW50PSJpbmRlZmluaXRlIiAvPjwvdGV4dFBhdGg+"
          "IDx0ZXh0UGF0aCBzdGFydE9mZnNldD0iMCUiIGZpbGw9IndoaXRlIiBmb250LWZhbWls"
          "eT0iJ0NvdXJpZXIgTmV3JywgbW9ub3NwYWNlIiBmb250LXNpemU9IjEwcHgiIHhsaW5r"
          "OmhyZWY9IiN0ZXh0LXBhdGgtYSI+"
          "MHhjMDJhYWEzOWIyMjNmZThkMGEwZTVjNGYyN2VhZDkwODNjNzU2Y2MyIOKAoiBXRVRI"
          "IDxhbmltYXRlIGFkZGl0aXZlPSJzdW0iIGF0dHJpYnV0ZU5hbWU9InN0YXJ0T2Zmc2V0"
          "IiBmcm9tPSIwJSIgdG89IjEwMCUiIGJlZ2luPSIwcyIgZHVyPSIzMHMiIHJlcGVhdENv"
          "dW50PSJpbmRlZmluaXRlIiAvPiA8L3RleHRQYXRoPjx0ZXh0UGF0aCBzdGFydE9mZnNl"
          "dD0iNTAlIiBmaWxsPSJ3aGl0ZSIgZm9udC1mYW1pbHk9IidDb3VyaWVyIE5ldycsIG1v"
          "bm9zcGFjZSIgZm9udC1zaXplPSIxMHB4IiB4bGluazpocmVmPSIjdGV4dC1wYXRoLWEi"
          "PjB4MWY5ODQwYTg1ZDVhZjViZjFkMTc2MmY5MjViZGFkZGM0MjAxZjk4NCDigKIgVU5J"
          "IDxhbmltYXRlIGFkZGl0aXZlPSJzdW0iIGF0dHJpYnV0ZU5hbWU9InN0YXJ0T2Zmc2V0"
          "IiBmcm9tPSIwJSIgdG89IjEwMCUiIGJlZ2luPSIwcyIgZHVyPSIzMHMiIHJlcGVhdENv"
          "dW50PSJpbmRlZmluaXRlIiAvPjwvdGV4dFBhdGg+"
          "PHRleHRQYXRoIHN0YXJ0T2Zmc2V0PSItNTAlIiBmaWxsPSJ3aGl0ZSIgZm9udC1mYW1p"
          "bHk9IidDb3VyaWVyIE5ldycsIG1vbm9zcGFjZSIgZm9udC1zaXplPSIxMHB4IiB4bGlu"
          "azpocmVmPSIjdGV4dC1wYXRoLWEiPjB4MWY5ODQwYTg1ZDVhZjViZjFkMTc2MmY5MjVi"
          "ZGFkZGM0MjAxZjk4NCDigKIgVU5JIDxhbmltYXRlIGFkZGl0aXZlPSJzdW0iIGF0dHJp"
          "YnV0ZU5hbWU9InN0YXJ0T2Zmc2V0IiBmcm9tPSIwJSIgdG89IjEwMCUiIGJlZ2luPSIw"
          "cyIgZHVyPSIzMHMiIHJlcGVhdENvdW50PSJpbmRlZmluaXRlIiAvPjwvdGV4dFBhdGg+"
          "PC90ZXh0PjxnIG1hc2s9InVybCgjZmFkZS1zeW1ib2wpIj48cmVjdCBmaWxsPSJub25l"
          "IiB4PSIwcHgiIHk9IjBweCIgd2lkdGg9IjI5MHB4IiBoZWlnaHQ9IjIwMHB4IiAvPiA8"
          "dGV4dCB5PSI3MHB4IiB4PSIzMnB4IiBmaWxsPSJ3aGl0ZSIgZm9udC1mYW1pbHk9IidD"
          "b3VyaWVyIE5ldycsIG1vbm9zcGFjZSIgZm9udC13ZWlnaHQ9IjIwMCIgZm9udC1zaXpl"
          "PSIzNnB4Ij5VTkkvV0VUSDwvdGV4dD48dGV4dCB5PSIxMTVweCIgeD0iMzJweCIgZmls"
          "bD0id2hpdGUiIGZvbnQtZmFtaWx5PSInQ291cmllciBOZXcnLCBtb25vc3BhY2UiIGZv"
          "bnQtd2VpZ2h0PSIyMDAiIGZvbnQtc2l6ZT0iMzZweCI+MC4zJTwvdGV4dD48L2c+"
          "PHJlY3QgeD0iMTYiIHk9IjE2IiB3aWR0aD0iMjU4IiBoZWlnaHQ9IjQ2OCIgcng9IjI2"
          "IiByeT0iMjYiIGZpbGw9InJnYmEoMCwwLDAsMCkiIHN0cm9rZT0icmdiYSgyNTUsMjU1"
          "LDI1NSwwLjIpIiAvPjxnIG1hc2s9InVybCgjZmFkZS1kb3duKSIgc3R5bGU9InRyYW5z"
          "Zm9ybTp0cmFuc2xhdGUoNzJweCwxODlweCkiPjxyZWN0IHg9Ii0xNnB4IiB5PSItMTZw"
          "eCIgd2lkdGg9IjE4MHB4IiBoZWlnaHQ9IjE4MHB4IiBmaWxsPSJub25lIiAvPjxwYXRo"
          "IGQ9Ik0xIDFDMSA4OSA1Ny41IDE0NSAxNDUgMTQ1IiBzdHJva2U9InJnYmEoMCwwLDAs"
          "MC4zKSIgc3Ryb2tlLXdpZHRoPSIzMnB4IiBmaWxsPSJub25lIiBzdHJva2UtbGluZWNh"
          "cD0icm91bmQiIC8+"
          "PC9nPjxnIG1hc2s9InVybCgjZmFkZS1kb3duKSIgc3R5bGU9InRyYW5zZm9ybTp0cmFu"
          "c2xhdGUoNzJweCwxODlweCkiPjxyZWN0IHg9Ii0xNnB4IiB5PSItMTZweCIgd2lkdGg9"
          "IjE4MHB4IiBoZWlnaHQ9IjE4MHB4IiBmaWxsPSJub25lIiAvPjxwYXRoIGQ9Ik0xIDFD"
          "MSA4OSA1Ny41IDE0NSAxNDUgMTQ1IiBzdHJva2U9InJnYmEoMjU1LDI1NSwyNTUsMSki"
          "IGZpbGw9Im5vbmUiIHN0cm9rZS1saW5lY2FwPSJyb3VuZCIgLz48L2c+"
          "PGNpcmNsZSBjeD0iNzNweCIgY3k9IjE5MHB4IiByPSI0cHgiIGZpbGw9IndoaXRlIiAv"
          "PjxjaXJjbGUgY3g9IjczcHgiIGN5PSIxOTBweCIgcj0iMjRweCIgZmlsbD0ibm9uZSIg"
          "c3Ryb2tlPSJ3aGl0ZSIgLz4gPGcgc3R5bGU9InRyYW5zZm9ybTp0cmFuc2xhdGUoMjlw"
          "eCwgMzg0cHgpIj48cmVjdCB3aWR0aD0iNjNweCIgaGVpZ2h0PSIyNnB4IiByeD0iOHB4"
          "IiByeT0iOHB4IiBmaWxsPSJyZ2JhKDAsMCwwLDAuNikiIC8+"
          "PHRleHQgeD0iMTJweCIgeT0iMTdweCIgZm9udC1mYW1pbHk9IidDb3VyaWVyIE5ldycs"
          "IG1vbm9zcGFjZSIgZm9udC1zaXplPSIxMnB4IiBmaWxsPSJ3aGl0ZSI+"
          "PHRzcGFuIGZpbGw9InJnYmEoMjU1LDI1NSwyNTUsMC42KSI+SUQ6IDwvdHNwYW4+"
          "MTwvdGV4dD48L2c+"
          "IDxnIHN0eWxlPSJ0cmFuc2Zvcm06dHJhbnNsYXRlKDI5cHgsIDQxNHB4KSI+"
          "PHJlY3Qgd2lkdGg9IjE0MHB4IiBoZWlnaHQ9IjI2cHgiIHJ4PSI4cHgiIHJ5PSI4cHgi"
          "IGZpbGw9InJnYmEoMCwwLDAsMC42KSIgLz48dGV4dCB4PSIxMnB4IiB5PSIxN3B4IiBm"
          "b250LWZhbWlseT0iJ0NvdXJpZXIgTmV3JywgbW9ub3NwYWNlIiBmb250LXNpemU9IjEy"
          "cHgiIGZpbGw9IndoaXRlIj48dHNwYW4gZmlsbD0icmdiYSgyNTUsMjU1LDI1NSwwLjYp"
          "Ij5NaW4gVGljazogPC90c3Bhbj4tNTA1ODA8L3RleHQ+"
          "PC9nPiA8ZyBzdHlsZT0idHJhbnNmb3JtOnRyYW5zbGF0ZSgyOXB4LCA0NDRweCkiPjxy"
          "ZWN0IHdpZHRoPSIxNDBweCIgaGVpZ2h0PSIyNnB4IiByeD0iOHB4IiByeT0iOHB4IiBm"
          "aWxsPSJyZ2JhKDAsMCwwLDAuNikiIC8+"
          "PHRleHQgeD0iMTJweCIgeT0iMTdweCIgZm9udC1mYW1pbHk9IidDb3VyaWVyIE5ldycs"
          "IG1vbm9zcGFjZSIgZm9udC1zaXplPSIxMnB4IiBmaWxsPSJ3aGl0ZSI+"
          "PHRzcGFuIGZpbGw9InJnYmEoMjU1LDI1NSwyNTUsMC42KSI+"
          "TWF4IFRpY2s6IDwvdHNwYW4+"
          "LTM2NzIwPC90ZXh0PjwvZz48ZyBzdHlsZT0idHJhbnNmb3JtOnRyYW5zbGF0ZSgyMjZw"
          "eCwgNDMzcHgpIj48cmVjdCB3aWR0aD0iMzZweCIgaGVpZ2h0PSIzNnB4IiByeD0iOHB4"
          "IiByeT0iOHB4IiBmaWxsPSJub25lIiBzdHJva2U9InJnYmEoMjU1LDI1NSwyNTUsMC4y"
          "KSIgLz48cGF0aCBzdHJva2UtbGluZWNhcD0icm91bmQiIGQ9Ik04IDlDOC4wMDAwNCAy"
          "Mi45NDk0IDE2LjIwOTkgMjggMjcgMjgiIGZpbGw9Im5vbmUiIHN0cm9rZT0id2hpdGUi"
          "IC8+"
          "PGNpcmNsZSBzdHlsZT0idHJhbnNmb3JtOnRyYW5zbGF0ZTNkKDhweCwgMTQuMjVweCwg"
          "MHB4KSIgY3g9IjBweCIgY3k9IjBweCIgcj0iNHB4IiBmaWxsPSJ3aGl0ZSIvPjwvZz48"
          "ZyBzdHlsZT0idHJhbnNmb3JtOnRyYW5zbGF0ZSgyMjZweCwgMzkycHgpIj48cmVjdCB3"
          "aWR0aD0iMzZweCIgaGVpZ2h0PSIzNnB4IiByeD0iOHB4IiByeT0iOHB4IiBmaWxsPSJu"
          "b25lIiBzdHJva2U9InJnYmEoMjU1LDI1NSwyNTUsMC4yKSIgLz48Zz48cGF0aCBzdHls"
          "ZT0idHJhbnNmb3JtOnRyYW5zbGF0ZSg2cHgsNnB4KSIgZD0iTTEyIDBMMTIuNjUyMiA5"
          "LjU2NTg3TDE4IDEuNjA3N0wxMy43ODE5IDEwLjIxODFMMjIuMzkyMyA2TDE0LjQzNDEg"
          "MTEuMzQ3OEwyNCAxMkwxNC40MzQxIDEyLjY1MjJMMjIuMzkyMyAxOEwxMy43ODE5IDEz"
          "Ljc4MTlMMTggMjIuMzkyM0wxMi42NTIyIDE0LjQzNDFMMTIgMjRMMTEuMzQ3OCAxNC40"
          "MzQxTDYgMjIuMzkyM0wxMC4yMTgxIDEzLjc4MTlMMS42MDc3IDE4TDkuNTY1ODcgMTIu"
          "NjUyMkwwIDEyTDkuNTY1ODcgMTEuMzQ3OEwxLjYwNzcgNkwxMC4yMTgxIDEwLjIxODFM"
          "NiAxLjYwNzdMMTEuMzQ3OCA5LjU2NTg3TDEyIDBaIiBmaWxsPSJ3aGl0ZSIgLz48YW5p"
          "bWF0ZVRyYW5zZm9ybSBhdHRyaWJ1dGVOYW1lPSJ0cmFuc2Zvcm0iIHR5cGU9InJvdGF0"
          "ZSIgZnJvbT0iMCAxOCAxOCIgdG89IjM2MCAxOCAxOCIgZHVyPSIxMHMiIHJlcGVhdENv"
          "dW50PSJpbmRlZmluaXRlIi8+PC9nPjwvZz48L3N2Zz4=\"}"));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  // Invalid supportsInterface response
  // Timeout (1/4)
  callback_called = false;
  SetERC721MetadataInterceptor(interface_supported_response,
                               https_token_uri_response, "",
                               net::HTTP_REQUEST_TIMEOUT);
  json_rpc_service_->GetERC721Metadata(
      "0xbc4ca0eda7647a8ab7c2061c2e118a18a936f13d", "0x719",
      mojom::kMainnetChainId,
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kInternalError,
                     l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR), ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  // Invalid JSON (2/4)
  callback_called = false;
  SetERC721MetadataInterceptor(invalid_json);
  json_rpc_service_->GetERC721Metadata(
      "0xbc4ca0eda7647a8ab7c2061c2e118a18a936f13d", "0x719",
      mojom::kMainnetChainId,
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kParsingError,
                     l10n_util::GetStringUTF8(IDS_WALLET_PARSING_ERROR), ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  // Request exceeds provider limit (3/4)
  callback_called = false;
  SetERC721MetadataInterceptor(exceeds_limit_json);
  json_rpc_service_->GetERC721Metadata(
      "0xbc4ca0eda7647a8ab7c2061c2e118a18a936f13d", "0x719",
      mojom::kMainnetChainId,
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kLimitExceeded,
                     "Request exceeds defined limit", ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  // Interface not supported (4/4)
  callback_called = false;
  SetERC721MetadataInterceptor(interface_not_supported_response);
  json_rpc_service_->GetERC721Metadata(
      "0xbc4ca0eda7647a8ab7c2061c2e118a18a936f13d", "0x719",
      mojom::kMainnetChainId,
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kInvalidParams,
                     l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS),
                     ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  // Invalid tokenURI response (3 total)
  // (1/3) Timeout
  callback_called = false;
  SetERC721MetadataInterceptor(interface_supported_response,
                               https_token_uri_response, "", net::HTTP_OK,
                               net::HTTP_REQUEST_TIMEOUT);
  json_rpc_service_->GetERC721Metadata(
      "0x59468516a8259058bad1ca5f8f4bff190d30e066", "0x719",
      mojom::kMainnetChainId,
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kInternalError,
                     l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR), ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  // (2/3) Invalid JSON
  callback_called = false;
  SetERC721MetadataInterceptor(interface_supported_response, invalid_json);
  json_rpc_service_->GetERC721Metadata(
      "0x59468516a8259058bad1ca5f8f4bff190d30e066", "0x719",
      mojom::kMainnetChainId,
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kParsingError,
                     l10n_util::GetStringUTF8(IDS_WALLET_PARSING_ERROR), ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  // (3/3) Request exceeds limit
  callback_called = false;
  SetERC721MetadataInterceptor(interface_supported_response,
                               exceeds_limit_json);
  json_rpc_service_->GetERC721Metadata(
      "0x59468516a8259058bad1ca5f8f4bff190d30e066", "0x719",
      mojom::kMainnetChainId,
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kLimitExceeded,
                     "Request exceeds defined limit", ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  // Invalid metadata response (3 total)
  // (1/2) Timeout
  callback_called = false;
  SetERC721MetadataInterceptor(interface_supported_response,
                               https_token_uri_response,
                               https_metadata_response, net::HTTP_OK,
                               net::HTTP_OK, net::HTTP_REQUEST_TIMEOUT);
  json_rpc_service_->GetERC721Metadata(
      "0x59468516a8259058bad1ca5f8f4bff190d30e066", "0x719",
      mojom::kMainnetChainId,
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kInternalError,
                     l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR), ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  // // (2) Invalid JSON
  // // TODO(nvonpentz): Finish test when
  // // https://github.com/brave/brave-core/pull/12685 is merged callback_called
  // =
  // // false; SetERC721MetadataInterceptor(
  // //     interface_supported_response,
  // //     ipfs_token_uri_response,
  // //     invalid_json);
  // // json_rpc_service_->GetERC721Metadata(
  // //     "0x59468516a8259058bad1ca5f8f4bff190d30e066", "0x719",
  // //     mojom::kMainnetChainId,
  // //     base::BindOnce(&OnStringResponse, &callback_called,
  // //                    mojom::ProviderError::kParsingError,
  // //                    l10n_util::GetStringUTF8(IDS_WALLET_PARSING_ERROR),
  // //                    ""));
  // // base::RunLoop().RunUntilIdle();
  // // EXPECT_TRUE(callback_called);
}

TEST_F(JsonRpcServiceUnitTest, GetERC721Balance) {
  bool callback_called = false;

  // Invalid inputs.
  json_rpc_service_->GetERC721TokenBalance(
      "", "0x1", "0x983110309620D911731Ac0932219af06091b6744",
      mojom::kMainnetChainId,
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kInvalidParams,
                     l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS),
                     ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  json_rpc_service_->GetERC721TokenBalance(
      "0x06012c8cf97BEaD5deAe237070F9587f8E7A266d", "",
      "0x983110309620D911731Ac0932219af06091b6744", mojom::kMainnetChainId,
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kInvalidParams,
                     l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS),
                     ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  json_rpc_service_->GetERC721TokenBalance(
      "0x06012c8cf97BEaD5deAe237070F9587f8E7A266d", "0x1", "",
      mojom::kMainnetChainId,
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kInvalidParams,
                     l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS),
                     ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  json_rpc_service_->GetERC721TokenBalance(
      "0x06012c8cf97BEaD5deAe237070F9587f8E7A266d", "0x1",
      "0x983110309620D911731Ac0932219af06091b6744", "",
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kInvalidParams,
                     l10n_util::GetStringUTF8(IDS_WALLET_INVALID_PARAMETERS),
                     ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  SetInterceptor(
      "eth_call", "",
      "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":"
      "\"0x000000000000000000000000983110309620d911731ac0932219af0609"
      "1b6744\"}");

  // Owner gets balance 0x1.
  callback_called = false;
  json_rpc_service_->GetERC721TokenBalance(
      "0x06012c8cf97BEaD5deAe237070F9587f8E7A266d", "0x1",
      "0x983110309620D911731Ac0932219af06091b6744", mojom::kMainnetChainId,
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kSuccess, "", "0x1"));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  // Non-checksum address can get the same balance.
  callback_called = false;
  json_rpc_service_->GetERC721TokenBalance(
      "0x06012c8cf97BEaD5deAe237070F9587f8E7A266d", "0x1",
      "0x983110309620d911731ac0932219af06091b6744", mojom::kMainnetChainId,
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kSuccess, "", "0x1"));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  // Non-owner gets balance 0x0.
  callback_called = false;
  json_rpc_service_->GetERC721TokenBalance(
      "0x06012c8cf97BEaD5deAe237070F9587f8E7A266d", "0x1",
      "0x983110309620d911731ac0932219af06091b7811", mojom::kMainnetChainId,
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kSuccess, "", "0x0"));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  SetHTTPRequestTimeoutInterceptor();
  json_rpc_service_->GetERC721TokenBalance(
      "0x06012c8cf97BEaD5deAe237070F9587f8E7A266d", "0x1",
      "0x983110309620d911731ac0932219af06091b6744", mojom::kMainnetChainId,
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kInternalError,
                     l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR), ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  SetInvalidJsonInterceptor();
  json_rpc_service_->GetERC721TokenBalance(
      "0x06012c8cf97BEaD5deAe237070F9587f8E7A266d", "0x1",
      "0x983110309620d911731ac0932219af06091b6744", mojom::kMainnetChainId,
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kParsingError,
                     l10n_util::GetStringUTF8(IDS_WALLET_PARSING_ERROR), ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  SetLimitExceededJsonErrorResponse();
  json_rpc_service_->GetERC721TokenBalance(
      "0x06012c8cf97BEaD5deAe237070F9587f8E7A266d", "0x1",
      "0x983110309620d911731ac0932219af06091b6744", mojom::kMainnetChainId,
      base::BindOnce(&OnStringResponse, &callback_called,
                     mojom::ProviderError::kLimitExceeded,
                     "Request exceeds defined limit", ""));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);
}

TEST_F(JsonRpcServiceUnitTest, GetSupportsInterface) {
  // Successful, and does support the interface
  bool callback_called = false;
  SetInterceptor("eth_call", "",
                 "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":"
                 "\"0x000000000000000000000000000000000000000000000000000000000"
                 "0000001\"}");
  json_rpc_service_->GetSupportsInterface(
      "0x06012c8cf97BEaD5deAe237070F9587f8E7A266d", "0x80ac58cd",
      base::BindOnce(&OnBoolResponse, &callback_called,
                     mojom::ProviderError::kSuccess, "", true));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  // Successful, but does not support the interface
  callback_called = false;
  SetInterceptor("eth_call", "",
                 "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":"
                 "\"0x000000000000000000000000000000000000000000000000000000000"
                 "0000000\"}");
  json_rpc_service_->GetSupportsInterface(
      "0x06012c8cf97BEaD5deAe237070F9587f8E7A266d", "0x80ac58cd",
      base::BindOnce(&OnBoolResponse, &callback_called,
                     mojom::ProviderError::kSuccess, "", false));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  // Invalid result, should be in hex form
  // todo can remove this one if we have checks for parsing errors
  callback_called = false;
  SetInterceptor("eth_call", "",
                 "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":\"0\"}");
  json_rpc_service_->GetSupportsInterface(
      "0x06012c8cf97BEaD5deAe237070F9587f8E7A266d", "0x80ac58cd",
      base::BindOnce(&OnBoolResponse, &callback_called,
                     mojom::ProviderError::kParsingError,
                     l10n_util::GetStringUTF8(IDS_WALLET_PARSING_ERROR),
                     false));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  SetHTTPRequestTimeoutInterceptor();
  json_rpc_service_->GetSupportsInterface(
      "0x06012c8cf97BEaD5deAe237070F9587f8E7A266d", "0x80ac58cd",
      base::BindOnce(&OnBoolResponse, &callback_called,
                     mojom::ProviderError::kInternalError,
                     l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR),
                     false));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  SetInvalidJsonInterceptor();
  json_rpc_service_->GetSupportsInterface(
      "0x06012c8cf97BEaD5deAe237070F9587f8E7A266d", "0x80ac58cd",
      base::BindOnce(&OnBoolResponse, &callback_called,
                     mojom::ProviderError::kParsingError,
                     l10n_util::GetStringUTF8(IDS_WALLET_PARSING_ERROR),
                     false));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);

  callback_called = false;
  SetLimitExceededJsonErrorResponse();
  json_rpc_service_->GetSupportsInterface(
      "0x06012c8cf97BEaD5deAe237070F9587f8E7A266d", "0x80ac58cd",
      base::BindOnce(&OnBoolResponse, &callback_called,
                     mojom::ProviderError::kLimitExceeded,
                     "Request exceeds defined limit", false));
  base::RunLoop().RunUntilIdle();
  EXPECT_TRUE(callback_called);
}

TEST_F(JsonRpcServiceUnitTest, Reset) {
  std::vector<base::Value> values;
  mojom::NetworkInfo chain("0x1", "chain_name", {"https://url1.com"},
                           {"https://url1.com"}, {"https://url1.com"},
                           "symbol_name", "symbol", 11, mojom::CoinType::ETH,
                           mojom::NetworkInfoData::NewEthData(
                               mojom::NetworkInfoDataETH::New(false)));
  auto chain_ptr = chain.Clone();
  values.push_back(brave_wallet::EthNetworkInfoToValue(chain_ptr));
  UpdateCustomNetworks(prefs(), &values);

  std::vector<mojom::NetworkInfoPtr> custom_chains;
  GetAllEthCustomChains(prefs(), &custom_chains);
  ASSERT_FALSE(custom_chains.empty());
  custom_chains.clear();
  ASSERT_TRUE(custom_chains.empty());
  EXPECT_TRUE(SetNetwork(mojom::kLocalhostChainId, mojom::CoinType::ETH));
  prefs()->SetBoolean(kSupportEip1559OnLocalhostChain, true);
  EXPECT_TRUE(prefs()->HasPrefPath(kBraveWalletCustomNetworks));
  EXPECT_EQ(GetCurrentChainId(prefs(), mojom::CoinType::ETH),
            mojom::kLocalhostChainId);
  // This isn't valid data for these maps but we are just checking to make sure
  // it gets cleared
  json_rpc_service_->add_chain_pending_requests_["1"] =
      mojom::NetworkInfo::New();
  json_rpc_service_->add_chain_pending_requests_origins_["1"] = GURL();
  json_rpc_service_->switch_chain_requests_[GURL()] = "";
  json_rpc_service_->switch_chain_callbacks_[GURL()] =
      base::BindLambdaForTesting(
          [&](base::Value id, base::Value formed_response, const bool reject,
              const std::string& first_allowed_account,
              const bool update_bind_js_properties) {});

  json_rpc_service_->Reset();

  GetAllEthCustomChains(prefs(), &custom_chains);
  ASSERT_TRUE(custom_chains.empty());
  EXPECT_FALSE(prefs()->HasPrefPath(kBraveWalletCustomNetworks));
  EXPECT_EQ(GetCurrentChainId(prefs(), mojom::CoinType::ETH),
            mojom::kMainnetChainId);
  EXPECT_FALSE(prefs()->HasPrefPath(kSupportEip1559OnLocalhostChain));
  EXPECT_TRUE(json_rpc_service_->add_chain_pending_requests_.empty());
  EXPECT_TRUE(json_rpc_service_->add_chain_pending_requests_origins_.empty());
  EXPECT_TRUE(json_rpc_service_->switch_chain_requests_.empty());
  EXPECT_TRUE(json_rpc_service_->switch_chain_callbacks_.empty());
}

TEST_F(JsonRpcServiceUnitTest, GetSolanaBalance) {
  SetInterceptor("getBalance", "",
                 "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":"
                 "{\"context\":{\"slot\":106921266},\"value\":513234116063}}");
  TestGetSolanaBalance(513234116063ULL, mojom::SolanaProviderError::kSuccess,
                       "");

  // Response parsing error
  SetInterceptor("getBalance", "",
                 "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":\"0\"}");
  TestGetSolanaBalance(0u, mojom::SolanaProviderError::kParsingError,
                       l10n_util::GetStringUTF8(IDS_WALLET_PARSING_ERROR));

  // JSON RPC error
  SetInterceptor("getBalance", "",
                 "{\"jsonrpc\":\"2.0\",\"id\":1,\"error\":"
                 "{\"code\":-32601, \"message\": \"method does not exist\"}}");
  TestGetSolanaBalance(0u, mojom::SolanaProviderError::kMethodNotFound,
                       "method does not exist");

  // HTTP error
  SetHTTPRequestTimeoutInterceptor();
  TestGetSolanaBalance(0u, mojom::SolanaProviderError::kInternalError,
                       l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR));
}

TEST_F(JsonRpcServiceUnitTest, GetSPLTokenAccountBalance) {
  SetInterceptor(
      "getTokenAccountBalance", "",
      "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":"
      "{\"context\":{\"slot\":1069},\"value\":{\"amount\":\"9864\","
      "\"decimals\":2,\"uiAmount\":98.64,\"uiAmountString\":\"98.64\"}}}");
  TestGetSPLTokenAccountBalance("9864", 2u, "98.64",
                                mojom::SolanaProviderError::kSuccess, "");

  // Response parsing error
  SetInterceptor("getTokenAccountBalance", "",
                 "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":\"0\"}");
  TestGetSPLTokenAccountBalance(
      "", 0u, "", mojom::SolanaProviderError::kParsingError,
      l10n_util::GetStringUTF8(IDS_WALLET_PARSING_ERROR));

  // JSON RPC error
  SetInterceptor("getTokenAccountBalance", "",
                 "{\"jsonrpc\":\"2.0\",\"id\":1,\"error\":"
                 "{\"code\":-32601, \"message\": \"method does not exist\"}}");
  TestGetSPLTokenAccountBalance("", 0u, "",
                                mojom::SolanaProviderError::kMethodNotFound,
                                "method does not exist");

  // HTTP error
  SetHTTPRequestTimeoutInterceptor();
  TestGetSPLTokenAccountBalance(
      "", 0u, "", mojom::SolanaProviderError::kInternalError,
      l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR));
}

TEST_F(JsonRpcServiceUnitTest, SendSolanaTransaction) {
  SetInterceptor(
      "sendTransaction", "",
      "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":"
      "\"2id3YC2jK9G5Wo2phDx4gJVAew8DcY5NAojnVuao8rkxwPYPe8cSwE5GzhEgJA2y8fVjDE"
      "o6iR6ykBvDxrTQrtpb\"}");

  TestSendSolanaTransaction(
      "2id3YC2jK9G5Wo2phDx4gJVAew8DcY5NAojnVuao8rkxwPYPe8cSwE5GzhEgJA2y8fVjDEo6"
      "iR6ykBvDxrTQrtpb",
      mojom::SolanaProviderError::kSuccess, "");

  // Response parsing error
  SetInterceptor("sendTransaction", "",
                 "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":0}");
  TestSendSolanaTransaction("", mojom::SolanaProviderError::kParsingError,
                            l10n_util::GetStringUTF8(IDS_WALLET_PARSING_ERROR));

  // JSON RPC error
  SetInterceptor("sendTransaction", "",
                 "{\"jsonrpc\":\"2.0\",\"id\":1,\"error\":"
                 "{\"code\":-32601, \"message\": \"method does not exist\"}}");
  TestSendSolanaTransaction("", mojom::SolanaProviderError::kMethodNotFound,
                            "method does not exist");

  // HTTP error
  SetHTTPRequestTimeoutInterceptor();
  TestSendSolanaTransaction(
      "", mojom::SolanaProviderError::kInternalError,
      l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR));
}

TEST_F(JsonRpcServiceUnitTest, GetSolanaLatestBlockhash) {
  SetInterceptor("getLatestBlockhash", "",
                 "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":"
                 "{\"context\":{\"slot\":1069},\"value\":{\"blockhash\":"
                 "\"EkSnNWid2cvwEVnVx9aBqawnmiCNiDgp3gUdkDPTKN1N\", "
                 "\"lastValidBlockHeight\":3090}}}");

  TestGetSolanaLatestBlockhash("EkSnNWid2cvwEVnVx9aBqawnmiCNiDgp3gUdkDPTKN1N",
                               mojom::SolanaProviderError::kSuccess, "");

  // Response parsing error
  SetInterceptor("getLatestBlockhash", "",
                 "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":\"0\"}");
  TestGetSolanaLatestBlockhash(
      "", mojom::SolanaProviderError::kParsingError,
      l10n_util::GetStringUTF8(IDS_WALLET_PARSING_ERROR));

  // JSON RPC error
  SetInterceptor("getLatestBlockhash", "",
                 "{\"jsonrpc\":\"2.0\",\"id\":1,\"error\":"
                 "{\"code\":-32601, \"message\": \"method does not exist\"}}");
  TestGetSolanaLatestBlockhash("", mojom::SolanaProviderError::kMethodNotFound,
                               "method does not exist");

  // HTTP error
  SetHTTPRequestTimeoutInterceptor();
  TestGetSolanaLatestBlockhash(
      "", mojom::SolanaProviderError::kInternalError,
      l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR));
}

TEST_F(JsonRpcServiceUnitTest, MigrateMultichainNetworks) {
  prefs()->ClearPref(kBraveWalletCustomNetworks);
  prefs()->ClearPref(kBraveWalletSelectedNetworks);

  absl::optional<base::Value> old_custom_networks = base::JSONReader::Read(R"([
    {
        "blockExplorerUrls": [
            "https://thaichain.io"
        ],
        "chainId": "0x7",
        "chainName": "ThaiChain",
        "iconUrls": [],
        "is_eip1559": false,
        "nativeCurrency": {
            "decimals": 18,
            "name": "ThaiChain Ether",
            "symbol": "TCH"
        },
        "rpcUrls": [
            "https://rpc.dome.cloud"
        ]
    },
    {
        "blockExplorerUrls": [
            "https://ubiqscan.io"
        ],
        "chainId": "0x8",
        "chainName": "Ubiq",
        "iconUrls": [],
        "is_eip1559": false,
        "nativeCurrency": {
            "decimals": 18,
            "name": "Ubiq Ether",
            "symbol": "UBQ"
        },
        "rpcUrls": [
            "https://rpc.octano.dev",
            "https://pyrus2.ubiqscan.io"
        ]
    }
  ])");
  prefs()->Set(kBraveWalletCustomNetworksDeprecated, *old_custom_networks);
  prefs()->SetString(kBraveWalletCurrentChainId, "0x3");

  JsonRpcService::MigrateMultichainNetworks(prefs());

  const base::Value* new_custom_networks =
      prefs()->GetDictionary(kBraveWalletCustomNetworks);
  ASSERT_TRUE(new_custom_networks);
  const base::Value* eth_custom_networks =
      new_custom_networks->FindKey(kEthereumPrefKey);
  ASSERT_TRUE(eth_custom_networks);
  EXPECT_EQ(*eth_custom_networks, *old_custom_networks);

  const base::Value* selected_networks =
      prefs()->GetDictionary(kBraveWalletSelectedNetworks);
  ASSERT_TRUE(selected_networks);
  const std::string* eth_selected_networks =
      selected_networks->FindStringKey(kEthereumPrefKey);
  ASSERT_TRUE(eth_selected_networks);
  EXPECT_EQ(*eth_selected_networks, "0x3");
  const std::string* sol_selected_networks =
      selected_networks->FindStringKey(kSolanaPrefKey);
  ASSERT_TRUE(sol_selected_networks);
  EXPECT_EQ(*sol_selected_networks, mojom::kSolanaMainnet);

  const std::string* fil_selected_networks =
      selected_networks->FindStringKey(kFilecoinPrefKey);
  ASSERT_TRUE(fil_selected_networks);
  EXPECT_EQ(*fil_selected_networks, mojom::kFilecoinMainnet);

  EXPECT_FALSE(prefs()->HasPrefPath(kBraveWalletCustomNetworksDeprecated));
  EXPECT_FALSE(prefs()->HasPrefPath(kBraveWalletCurrentChainId));
}

TEST_F(JsonRpcServiceUnitTest, GetSolanaSignatureStatuses) {
  std::string json = R"(
      {"jsonrpc":2.0, "id":1, "result":
        {
          "context": {"slot": 82},
          "value": [
            {
              "slot": 9007199254740991,
              "confirmations": 10,
              "err": null,
              "confirmationStatus": "confirmed"
            },
            {
              "slot": 72,
              "confirmations": 9007199254740991,
              "err": null,
              "confirmationStatus": "confirmed"
            },
            {
              "slot": 1092,
              "confirmations": null,
              "err": {"InstructionError":[0,{"Custom":1}]},
              "confirmationStatus": "finalized"
            },
            null
          ]
        }
      }
  )";
  SetInterceptor("getSignatureStatuses", "", json);

  std::vector<std::string> tx_sigs = {
      "5VERv8NMvzbJMEkV8xnrLkEaWRtSz9CosKDYjCJjBRnbJLgp8uirBgmQpjKhoR4tjF3ZpRzr"
      "FmBV6UjKdiSZkQUW",
      "5j7s6NiJS3JAkvgkoc18WVAsiSaci2pxB2A6ueCJP4tprA2TFg9wSyTLeYouxPBJEMzJinEN"
      "TkpA52YStRW5Dia7",
      "4VERv8NMvzbJMEkV8xnrLkEaWRtSz9CosKDYjCJjBRnbJLgp8uirBgmQpjKhoR4tjF3ZpRzr"
      "FmBV6UjKdiSZkQUW",
      "45j7s6NiJS3JAkvgkoc18WVAsiSaci2pxB2A6ueCJP4tprA2TFg9wSyTLeYouxPBJEMzJinE"
      "NTkpA52YStRW5Dia7"};

  std::vector<absl::optional<SolanaSignatureStatus>> expected_statuses(
      {SolanaSignatureStatus(kMaxSafeIntegerUint64, 10u, "", "confirmed"),
       SolanaSignatureStatus(72u, kMaxSafeIntegerUint64, "", "confirmed"),
       SolanaSignatureStatus(
           1092u, 0u, R"({"InstructionError":[0,{"Custom":1}]})", "finalized"),
       absl::nullopt});
  TestGetSolanaSignatureStatuses(tx_sigs, expected_statuses,
                                 mojom::SolanaProviderError::kSuccess, "");

  // Response parsing error
  SetInterceptor("getSignatureStatuses", "",
                 "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":\"0\"}");
  TestGetSolanaSignatureStatuses(
      tx_sigs, std::vector<absl::optional<SolanaSignatureStatus>>(),
      mojom::SolanaProviderError::kParsingError,
      l10n_util::GetStringUTF8(IDS_WALLET_PARSING_ERROR));

  // JSON RPC error
  SetInterceptor("getSignatureStatuses", "",
                 "{\"jsonrpc\":\"2.0\",\"id\":1,\"error\":"
                 "{\"code\":-32601, \"message\": \"method does not exist\"}}");
  TestGetSolanaSignatureStatuses(
      tx_sigs, std::vector<absl::optional<SolanaSignatureStatus>>(),
      mojom::SolanaProviderError::kMethodNotFound, "method does not exist");

  // HTTP error
  SetHTTPRequestTimeoutInterceptor();
  TestGetSolanaSignatureStatuses(
      tx_sigs, std::vector<absl::optional<SolanaSignatureStatus>>(),
      mojom::SolanaProviderError::kInternalError,
      l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR));
}

TEST_F(JsonRpcServiceUnitTest, GetSolanaAccountInfo) {
  std::string json = R"(
    {
      "jsonrpc":"2.0","id":1,
      "result": {
        "context":{"slot":123065869},
        "value":{
          "data":["SEVMTE8gV09STEQ=","base64"],
          "executable":false,
          "lamports":88801034809120,
          "owner":"11111111111111111111111111111111",
          "rentEpoch":284
        }
      }
    }
  )";
  SetInterceptor("getAccountInfo", "", json);

  SolanaAccountInfo expected_info;
  expected_info.lamports = 88801034809120ULL;
  expected_info.owner = "11111111111111111111111111111111";
  expected_info.data = "SEVMTE8gV09STEQ=";
  expected_info.executable = false;
  expected_info.rent_epoch = 284;
  TestGetSolanaAccountInfo(expected_info, mojom::SolanaProviderError::kSuccess,
                           "");

  // value can be null for an account not on chain.
  SetInterceptor(
      "getAccountInfo", "",
      R"({"jsonrpc":"2.0","result":{"context":{"slot":123121238},"value":null},"id":1})");
  TestGetSolanaAccountInfo(absl::nullopt, mojom::SolanaProviderError::kSuccess,
                           "");

  // Response parsing error
  SetInterceptor("getAccountInfo", "",
                 "{\"jsonrpc\":\"2.0\",\"id\":1,\"result\":\"0\"}");
  TestGetSolanaAccountInfo(absl::nullopt,
                           mojom::SolanaProviderError::kParsingError,
                           l10n_util::GetStringUTF8(IDS_WALLET_PARSING_ERROR));

  // JSON RPC error
  SetInterceptor("getAccountInfo", "",
                 "{\"jsonrpc\":\"2.0\",\"id\":1,\"error\":"
                 "{\"code\":-32601, \"message\": \"method does not exist\"}}");
  TestGetSolanaAccountInfo(absl::nullopt,
                           mojom::SolanaProviderError::kMethodNotFound,
                           "method does not exist");

  // HTTP error
  SetHTTPRequestTimeoutInterceptor();
  TestGetSolanaAccountInfo(absl::nullopt,
                           mojom::SolanaProviderError::kInternalError,
                           l10n_util::GetStringUTF8(IDS_WALLET_INTERNAL_ERROR));
}

}  // namespace brave_wallet
