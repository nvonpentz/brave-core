/* Copyright (c) 2022 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "brave/components/brave_wallet/browser/fil_tx_state_manager.h"

#include <memory>
#include <utility>
#include <vector>

#include "base/test/bind.h"
#include "base/test/task_environment.h"
#include "brave/components/brave_wallet/browser/brave_wallet_prefs.h"
#include "brave/components/brave_wallet/browser/fil_transaction.h"
#include "brave/components/brave_wallet/browser/fil_tx_meta.h"
#include "brave/components/brave_wallet/common/brave_wallet.mojom.h"
#include "components/sync_preferences/testing_pref_service_syncable.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace brave_wallet {

class FilTxStateManagerUnitTest : public testing::Test {
 public:
  FilTxStateManagerUnitTest()
      : task_environment_(base::test::TaskEnvironment::TimeSource::MOCK_TIME) {}
  ~FilTxStateManagerUnitTest() override = default;

 protected:
  void SetUp() override {
    brave_wallet::RegisterProfilePrefs(prefs_.registry());
    fil_tx_state_manager_ = std::make_unique<FilTxStateManager>(GetPrefs());
  }

  PrefService* GetPrefs() { return &prefs_; }

  base::test::TaskEnvironment task_environment_;
  sync_preferences::TestingPrefServiceSyncable prefs_;
  std::unique_ptr<FilTxStateManager> fil_tx_state_manager_;
};

TEST_F(FilTxStateManagerUnitTest, FilTxMetaAndValue) {
  const std::string to = "t1h4n7rphclbmwyjcp6jrdiwlfcuwbroxy3jvg33q";
  const std::string from = "t1h5tg3bhp5r56uzgjae2373znti6ygq4agkx4hzq";
  auto tx = std::make_unique<FilTransaction>();
  tx->set_nonce(1);
  tx->set_gas_premium("2");
  tx->set_fee_cap("3");
  tx->set_gas_limit(4);
  tx->set_max_fee("5");
  tx->set_to(FilAddress::FromAddress(to));
  tx->set_value("6");

  FilTxMeta meta(std::move(tx));
  meta.set_id(TxMeta::GenerateMetaID());
  meta.set_status(mojom::TransactionStatus::Submitted);
  meta.set_from(from);
  meta.set_created_time(base::Time::Now());
  meta.set_submitted_time(base::Time::Now());
  meta.set_confirmed_time(base::Time::Now());
  meta.set_tx_hash("cid");
  meta.set_origin(url::Origin::Create(GURL("https://test.brave.com")));
  meta.set_chain_id(mojom::kFilecoinMainnet);

  base::Value::Dict meta_value = meta.ToValue();
  auto meta_from_value = fil_tx_state_manager_->ValueToFilTxMeta(meta_value);
  ASSERT_TRUE(meta_from_value);
  EXPECT_EQ(*meta_from_value, meta);
}

TEST_F(FilTxStateManagerUnitTest, GetTxPrefPathPrefix) {
  EXPECT_EQ("filecoin.mainnet", fil_tx_state_manager_->GetTxPrefPathPrefix(
                                    mojom::kFilecoinMainnet));
  EXPECT_EQ("filecoin.testnet", fil_tx_state_manager_->GetTxPrefPathPrefix(
                                    mojom::kFilecoinTestnet));
  EXPECT_EQ(
      "filecoin.http://localhost:1234/rpc/v0",
      fil_tx_state_manager_->GetTxPrefPathPrefix(mojom::kLocalhostChainId));
  EXPECT_EQ("filecoin",
            fil_tx_state_manager_->GetTxPrefPathPrefix(absl::nullopt));
}

}  // namespace brave_wallet
