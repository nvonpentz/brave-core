/* Copyright (c) 2021 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "brave/components/brave_wallet/browser/filecoin_keyring.h"

#include <utility>

#include "base/base64.h"
#include "base/json/json_reader.h"
#include "base/strings/string_number_conversions.h"
#include "brave/components/brave_wallet/browser/eth_address.h"
#include "brave/components/brave_wallet/browser/eth_transaction.h"
#include "brave/components/brave_wallet/common/hash_utils.h"
#include "brave/third_party/argon2/src/src/blake2/blake2.h"
#include "components/base32/base32.h"

namespace {

std::vector<uint8_t> GetBlakeHash(const std::vector<uint8_t>& payload,
                                  size_t length) {
  blake2b_state blakeState;
  if (blake2b_init(&blakeState, length) != 0) {
    VLOG(0) << __func__ << ": blake2b_init failed";
    return std::vector<uint8_t>();
  }
  if (blake2b_update(&blakeState, payload.data(), payload.size()) != 0) {
    VLOG(0) << __func__ << ": blake2b_update failed";
    return std::vector<uint8_t>();
  }
  std::vector<uint8_t> result;
  result.resize(length);
  if (blake2b_final(&blakeState, result.data(), length) != 0) {
    VLOG(0) << __func__ << ": blake2b_final failed";
    return result;
  }
  return result;
}

}  // namespace

namespace brave_wallet {

FilecoinKeyring::FilecoinKeyring() = default;
FilecoinKeyring::~FilecoinKeyring() = default;

FilecoinKeyring::Type FilecoinKeyring::type() const {
  return kDefault;
}

std::string FilecoinKeyring::ImportAccount(
    const std::vector<uint8_t>& private_key) {
  if (private_key.empty()) {
    return std::string();
  }
  std::string json(private_key.begin(), private_key.end());
  if (json.empty()) {
    return std::string();
  }
  base::JSONReader::ValueWithError parsed_json =
      base::JSONReader::ReadAndReturnValueWithError(json);
  if (!parsed_json.value) {
    VLOG(0) << __func__ << ": Filecoin payload json parsed failed because "
            << parsed_json.error_message;
    return std::string();
  }

  const auto* privateKeyValue = parsed_json.value->FindStringKey("PrivateKey");
  if (!privateKeyValue || privateKeyValue->empty()) {
    VLOG(0) << __func__ << ": missing private key value";
    return std::string();
  }
  const auto* typeValue = parsed_json.value->FindStringKey("Type");
  if (!typeValue || typeValue->empty()) {
    VLOG(0) << __func__ << ": missing private key type value";
    return std::string();
  }
  std::string type = *typeValue;
  if (type != "secp256k1") {
    VLOG(0) << __func__ << ": unsupported encoding " << type;
    return std::string();
  }
  std::string privateKey = *privateKeyValue;
  std::string decoded_key;
  base::Base64Decode(privateKey, &decoded_key);
  std::vector<uint8_t> key(decoded_key.begin(), decoded_key.end());
  std::unique_ptr<HDKey> hd_key = HDKey::GenerateFromPrivateKey(key);
  if (!hd_key)
    return std::string();
  auto address = GetAddressInternal(hd_key.get());
  LOG(ERROR) << "address:" << address;
  // Account already exists
  if (imported_accounts_[address])
    return std::string();
  // Check if it is duplicate in derived accounts
  for (size_t i = 0; i < accounts_.size(); ++i) {
    if (GetAddress(i) == address)
      return std::string();
  }

  imported_accounts_[address] = std::move(hd_key);
  return address;
}

std::string FilecoinKeyring::GetAddress(size_t index) const {
  if (accounts_.empty() || index >= accounts_.size())
    return std::string();
  return GetAddressInternal(accounts_[index].get());
}

std::string FilecoinKeyring::GetAddressInternal(const HDKey* hd_key) const {
  if (!hd_key)
    return std::string();
  auto uncompressed_public_key = hd_key->GetUncompressedPublicKey();
  auto payload = GetBlakeHash(uncompressed_public_key, 20);
  std::vector<uint8_t> checksumPayload(payload);
  checksumPayload.insert(checksumPayload.begin(), 1);
  auto checksum = GetBlakeHash(checksumPayload, 4);
  std::vector<uint8_t> final(payload);
  final.insert(final.end(), checksum.begin(), checksum.end());
  std::string input(final.begin(), final.end());
  std::string encoded_output = base::ToLowerASCII(
      base32::Base32Encode(input, base32::Base32EncodePolicy::OMIT_PADDING));
  return "t1" + encoded_output;
}

size_t FilecoinKeyring::GetImportedAccountsNumber() const {
  return imported_accounts_.size();
}

bool FilecoinKeyring::RemoveImportedAccount(const std::string& address) {
  return imported_accounts_.erase(address) != 0;
}

HDKey* FilecoinKeyring::GetHDKeyFromAddress(const std::string& address) {
  const auto imported_accounts_iter = imported_accounts_.find(address);
  if (imported_accounts_iter != imported_accounts_.end())
    return imported_accounts_iter->second.get();
  for (size_t i = 0; i < accounts_.size(); ++i) {
    if (GetAddress(i) == address)
      return accounts_[i].get();
  }
  return nullptr;
}

}  // namespace brave_wallet
