/* Copyright (c) 2021 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "brave/components/brave_wallet/browser/filecoin_keyring.h"

#include "base/strings/string_number_conversions.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace brave_wallet {

TEST(FilecoinKeyring, ImportAccount) {
  std::string private_key_hex =
      "7b2254797065223a22736563703235366b31222c22507269766174654b6579223a22397a"
      "476d306335784e4359494b4b452f517976756d7255444339766e706146344774614f646c"
      "723459514d3d227d";
  std::vector<uint8_t> private_key;
  ASSERT_TRUE(base::HexStringToBytes(private_key_hex, &private_key));

  FilecoinKeyring keyring;
  ASSERT_TRUE(keyring.GetAddress(0).empty());
  auto address = keyring.ImportAccount(private_key);
  EXPECT_EQ(address, "t1gfpgfwrxdntcmcfqm7epsxrmsxlmsryvjkgat3i");
  EXPECT_EQ(keyring.GetImportedAccountsNumber(), size_t(1));
}

}  // namespace brave_wallet
