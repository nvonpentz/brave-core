/* Copyright (c) 2021 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_BRAVE_WALLET_BROWSER_FILECOIN_KEYRING_H_
#define BRAVE_COMPONENTS_BRAVE_WALLET_BROWSER_FILECOIN_KEYRING_H_

#include "brave/components/brave_wallet/browser/hd_key.h"

#include <memory>
#include <string>
#include <vector>

#include "base/containers/flat_map.h"
#include "base/gtest_prod_util.h"
#include "brave/components/brave_wallet/common/brave_wallet_types.h"
#include "brave/components/brave_wallet/browser/hd_keyring.h"
#include "third_party/abseil-cpp/absl/types/optional.h"

namespace brave_wallet {

class EthTransaction;

class FilecoinKeyring: public HDKeyring {
 public:
  FilecoinKeyring();
  ~FilecoinKeyring() override;

  Type type() const override;

  // address will be returned
  std::string ImportAccount(const std::vector<uint8_t>& private_key) override;
  size_t GetImportedAccountsNumber() const;
  bool RemoveImportedAccount(const std::string& address);
  std::string GetAddress(size_t index) const override;
  HDKey* GetHDKeyFromAddress(const std::string& address);

 protected:
  std::string GetAddressInternal(const HDKey* hd_key) const;

  std::unique_ptr<HDKey> root_;
  std::unique_ptr<HDKey> master_key_;
  // (address, key)
  base::flat_map<std::string, std::unique_ptr<HDKey>> imported_accounts_;

 private:
  friend FilecoinKeyring;
  FilecoinKeyring(const FilecoinKeyring&) = delete;
  FilecoinKeyring& operator=(const FilecoinKeyring&) = delete;
};

}  // namespace brave_wallet

#endif  // BRAVE_COMPONENTS_BRAVE_WALLET_BROWSER_FILECOIN_KEYRING_H_
