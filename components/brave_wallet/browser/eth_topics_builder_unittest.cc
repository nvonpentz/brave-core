/* Copyright (c) 2022 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "brave/components/brave_wallet/browser/eth_topics_builder.h"

#include <iostream>
#include "brave/components/brave_wallet/common/hex_utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace brave_wallet {

TEST(EthGetLogsTopicBuilderTest, MakeAssetDiscoveryTopics) {
  // Invalid address
  base::Value topics(base::Value::Type::LIST);
  ASSERT_FALSE(MakeAssetDiscoveryTopics({"invalid address"}, &topics));

  // Valid
  topics = base::Value(base::Value::Type::LIST);
  ASSERT_TRUE(MakeAssetDiscoveryTopics(
      {"0x16e4476c8fDDc552e3b1C4b8b56261d85977fE52"}, &topics));
  EXPECT_EQ(topics.GetList()[0], base::Value("0xddf252ad"));
  EXPECT_EQ(topics.GetList()[1], base::Value());
  base::Value to_address_topic(base::Value::Type::LIST);
  to_address_topic.Append(base::Value(
      "0x00000000000000000000000016e4476c8fDDc552e3b1C4b8b56261d85977fE52"));
  EXPECT_EQ(topics.GetList()[2], to_address_topic);
}

}  // namespace brave_wallet
