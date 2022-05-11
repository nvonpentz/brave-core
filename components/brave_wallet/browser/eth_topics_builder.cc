/* Copyright (c) 2022 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "brave/components/brave_wallet/browser/eth_topics_builder.h"
#include <utility>
#include "brave/components/brave_wallet/browser/brave_wallet_utils.h"

#include "brave/components/brave_wallet/common/hash_utils.h"
#include "brave/components/brave_wallet/common/hex_utils.h"

namespace brave_wallet {

bool MakeAssetDiscoveryTopics(
    const std::vector<std::string>& to_account_addresses,
    base::Value* topics) {
  // First topic matches erc20::Transfer function hash
  topics->Append(
      brave_wallet::GetFunctionHash("Transfer(address,address,uint256)"));

  // Second topic matches everything (any from address)
  topics->Append(base::Value());

  // Third topic matches any of the to_addresses
  base::Value to_address_topic(base::Value::Type::LIST);
  for (const auto& account_address : to_account_addresses) {
    std::string padded_address;
    if (!brave_wallet::PadHexEncodedParameter(account_address,
                                              &padded_address)) {
      return false;
    }
    to_address_topic.Append(padded_address);
  }
  topics->Append(std::move(to_address_topic));
  return true;
}

}  // namespace brave_wallet
