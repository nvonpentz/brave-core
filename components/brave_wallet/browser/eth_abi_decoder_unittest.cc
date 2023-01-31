/* Copyright (c) 2022 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "brave/components/brave_wallet/browser/eth_abi_decoder.h"

#include <string>
#include <vector>

#include "brave/components/brave_wallet/browser/brave_wallet_utils.h"
#include "brave/components/brave_wallet/common/hex_utils.h"
#include "testing/gtest/include/gtest/gtest.h"

namespace brave_wallet {

TEST(EthABIDecoderTest, ABIDecodeAddress) {
  std::vector<std::string> tx_params;
  std::vector<std::string> tx_args;

  std::vector<uint8_t> data;
  ASSERT_TRUE(PrefixedHexStringToBytes(
      "0x000000000000000000000000BFb30a082f650C2A15D0632f0e87bE4F8e64460f",
      &data));
  auto decoded = ABIDecode({"address"}, data);
  ASSERT_NE(decoded, absl::nullopt);
  std::tie(tx_params, tx_args) = *decoded;

  ASSERT_EQ(tx_params.size(), 1UL);
  EXPECT_EQ(tx_params[0], "address");
  EXPECT_EQ(tx_args[0], "0xbfb30a082f650c2a15d0632f0e87be4f8e64460f");

  // Insufficient address length
  ASSERT_TRUE(PrefixedHexStringToBytes(
      "0x000000000000000000000000BFb30a082f650C2A15D0632f0e87bE4F8e64", &data));
  ASSERT_FALSE(ABIDecode({"address"}, data));
}

TEST(EthABIDecoderTest, ABIDecodeUint256) {
  std::vector<std::string> tx_params;
  std::vector<std::string> tx_args;
  std::vector<uint8_t> data;

  // OK: 32-bytes well-formed uint256
  ASSERT_TRUE(PrefixedHexStringToBytes(
      "0x00000000000000000000000000000000000000000000000000000000000000ff",
      &data));
  auto decoded = ABIDecode({"uint256"}, data);
  ASSERT_NE(decoded, absl::nullopt);
  std::tie(tx_params, tx_args) = *decoded;
  ASSERT_EQ(tx_params.size(), 1UL);
  EXPECT_EQ(tx_params[0], "uint256");
  EXPECT_EQ(tx_args[0], "0xff");

  // KO: insufficient uint256 length
  ASSERT_TRUE(PrefixedHexStringToBytes("0xff", &data));
  ASSERT_FALSE(ABIDecode({"uint256"}, data));

  // OK: extra uint256 length
  ASSERT_TRUE(PrefixedHexStringToBytes(
      "0x00000000000000000000000000000000000000000000000000000000000000ff"
      "ff",
      &data));
  decoded = ABIDecode({"uint256"}, data);
  ASSERT_NE(decoded, absl::nullopt);
  std::tie(tx_params, tx_args) = *decoded;
  ASSERT_EQ(tx_params.size(), 1UL);
  EXPECT_EQ(tx_params[0], "uint256");
  EXPECT_EQ(tx_args[0], "0xff");
}

TEST(EthABIDecoderTest, ABIDecodeBool) {
  std::vector<std::string> tx_params;
  std::vector<std::string> tx_args;
  std::vector<uint8_t> data;

  // OK: false
  ASSERT_TRUE(PrefixedHexStringToBytes(
      "0x0000000000000000000000000000000000000000000000000000000000000000",
      &data));
  auto decoded = ABIDecode({"bool"}, data);
  ASSERT_NE(decoded, absl::nullopt);
  std::tie(tx_params, tx_args) = *decoded;
  ASSERT_EQ(tx_params.size(), 1UL);
  EXPECT_EQ(tx_params[0], "bool");
  EXPECT_EQ(tx_args[0], "false");

  // OK: true
  ASSERT_TRUE(PrefixedHexStringToBytes(
      "0x0000000000000000000000000000000000000000000000000000000000000001",
      &data));
  decoded = ABIDecode({"bool"}, data);
  ASSERT_NE(decoded, absl::nullopt);
  std::tie(tx_params, tx_args) = *decoded;
  ASSERT_EQ(tx_params.size(), 1UL);
  EXPECT_EQ(tx_params[0], "bool");
  EXPECT_EQ(tx_args[0], "true");

  // KO: insufficient bool length
  ASSERT_TRUE(PrefixedHexStringToBytes("0x0", &data));
  ASSERT_FALSE(ABIDecode({"bool"}, data));

  // OK: extra bool length
  ASSERT_TRUE(PrefixedHexStringToBytes(
      "0x0000000000000000000000000000000000000000000000000000000000000000"
      "00",
      &data));
  decoded = ABIDecode({"bool"}, data);
  ASSERT_NE(decoded, absl::nullopt);
  std::tie(tx_params, tx_args) = *decoded;
  ASSERT_EQ(tx_params.size(), 1UL);
  EXPECT_EQ(tx_params[0], "bool");
  EXPECT_EQ(tx_args[0], "false");
}

TEST(EthABIDecoderTest, ABIDecodeAddressArray) {
  std::vector<std::string> tx_params;
  std::vector<std::string> tx_args;

  std::vector<uint8_t> data;
  ASSERT_TRUE(PrefixedHexStringToBytes(
      "0x0000000000000000000000000000000000000000000000000000000000000020"
      "0000000000000000000000000000000000000000000000000000000000000002"
      "00000000000000000000000000000000000000000000000000000000000000ff"
      "0000000000000000000000000000000000000000000000000000000000000fff",
      &data));
  auto decoded = ABIDecode({"address[]"}, data);
  ASSERT_NE(decoded, absl::nullopt);
  std::tie(tx_params, tx_args) = *decoded;

  ASSERT_EQ(tx_params.size(), 1UL);
  EXPECT_EQ(tx_params[0], "address[]");
  EXPECT_EQ(tx_args[0],
            "0x00000000000000000000000000000000000000ff"
            "0000000000000000000000000000000000000fff");

  // Empty address
  ASSERT_TRUE(PrefixedHexStringToBytes(
      "0x0000000000000000000000000000000000000000000000000000000000000020"
      "0000000000000000000000000000000000000000000000000000000000000000",
      &data));
  decoded = ABIDecode({"address[]"}, data);
  ASSERT_NE(decoded, absl::nullopt);
  std::tie(tx_params, tx_args) = *decoded;

  ASSERT_EQ(tx_params.size(), 1UL);
  EXPECT_EQ(tx_params[0], "address[]");
  EXPECT_EQ(tx_args[0], "0x");

  // Valid data with extra tail calldata
  ASSERT_TRUE(PrefixedHexStringToBytes(
      "0x0000000000000000000000000000000000000000000000000000000000000020"
      "0000000000000000000000000000000000000000000000000000000000000002"
      "00000000000000000000000000000000000000000000000000000000000000ff"
      "0000000000000000000000000000000000000000000000000000000000000fff"
      "ffff",
      &data));
  decoded = ABIDecode({"address[]"}, data);
  ASSERT_NE(decoded, absl::nullopt);
  std::tie(tx_params, tx_args) = *decoded;

  ASSERT_EQ(tx_params.size(), 1UL);
  EXPECT_EQ(tx_params[0], "address[]");
  EXPECT_EQ(tx_args[0],
            "0x00000000000000000000000000000000000000ff"
            "0000000000000000000000000000000000000fff");

  // Invalid offset (out of calldata range)
  ASSERT_TRUE(PrefixedHexStringToBytes(
      "0x00000000000000000000000000000000000000000000000000000000000000ff",
      &data));
  ASSERT_FALSE(ABIDecode({"address[]"}, data));

  // Invalid offset (number too large)
  ASSERT_TRUE(PrefixedHexStringToBytes(
      "0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
      "0000000000000000000000000000000000000000000000000000000000000000",
      &data));
  ASSERT_FALSE(ABIDecode({"address[]"}, data));

  // Invalid array length (insufficient number of elements)
  ASSERT_TRUE(PrefixedHexStringToBytes(
      "0x0000000000000000000000000000000000000000000000000000000000000020"
      "00000000000000000000000000000000000000000000000000000000000000ff"
      "0000000000000000000000000000000000000000000000000000000000000000",
      &data));
  ASSERT_FALSE(ABIDecode({"address[]"}, data));

  // Invalid array length (number too large)
  ASSERT_TRUE(PrefixedHexStringToBytes(
      "0x0000000000000000000000000000000000000000000000000000000000000020"
      "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
      &data));
  ASSERT_FALSE(ABIDecode({"address[]"}, data));

  // Invalid address array contents
  ASSERT_TRUE(PrefixedHexStringToBytes(
      "0x00000000000000000000000000000000000000000000000000000000000000ff"
      "0000000000000000000000000000000000000000000000000000000000000001"
      "ffff",
      &data));
  ASSERT_FALSE(ABIDecode({"address[]"}, data));
}

TEST(EthABIDecoderTest, ABIDecodeBytes) {
  std::vector<std::string> tx_params;
  std::vector<std::string> tx_args;

  std::vector<uint8_t> data;
  ASSERT_TRUE(PrefixedHexStringToBytes(
      "0x0000000000000000000000000000000000000000000000000000000000000020"
      "0000000000000000000000000000000000000000000000000000000000000002"
      "ffff",
      &data));
  auto decoded = ABIDecode({"bytes"}, data);
  ASSERT_NE(decoded, absl::nullopt);
  std::tie(tx_params, tx_args) = *decoded;

  ASSERT_EQ(tx_params.size(), 1UL);
  EXPECT_EQ(tx_params[0], "bytes");
  EXPECT_EQ(tx_args[0], "0xffff");

  // Valid data with extra tail calldata
  ASSERT_TRUE(PrefixedHexStringToBytes(
      "0x0000000000000000000000000000000000000000000000000000000000000020"
      "0000000000000000000000000000000000000000000000000000000000000002"
      "ffff"
      "ffffff",  // extraneous tail data,
      &data));
  decoded = ABIDecode({"bytes"}, data);
  ASSERT_NE(decoded, absl::nullopt);
  std::tie(tx_params, tx_args) = *decoded;

  ASSERT_EQ(tx_params.size(), 1UL);
  EXPECT_EQ(tx_params[0], "bytes");
  EXPECT_EQ(tx_args[0], "0xffff");

  // Empty bytes
  ASSERT_TRUE(PrefixedHexStringToBytes(
      "0x0000000000000000000000000000000000000000000000000000000000000020"
      "0000000000000000000000000000000000000000000000000000000000000000",
      &data));
  decoded = ABIDecode({"bytes"}, data);
  ASSERT_NE(decoded, absl::nullopt);
  std::tie(tx_params, tx_args) = *decoded;

  ASSERT_EQ(tx_params.size(), 1UL);
  EXPECT_EQ(tx_params[0], "bytes");
  EXPECT_EQ(tx_args[0], "0x");

  // Invalid offset (out of range)
  ASSERT_TRUE(PrefixedHexStringToBytes(
      "0x00000000000000000000000000000000000000000000000000000000000000ff",
      &data));
  ASSERT_FALSE(ABIDecode({"bytes"}, data));

  // Invalid offset (number too large)
  ASSERT_TRUE(PrefixedHexStringToBytes(
      "0xffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
      "0000000000000000000000000000000000000000000000000000000000000000",
      &data));
  ASSERT_FALSE(ABIDecode({"bytes"}, data));

  // Invalid bytes length
  ASSERT_TRUE(PrefixedHexStringToBytes(
      "0x0000000000000000000000000000000000000000000000000000000000000020"
      "00000000000000000000000000000000000000000000000000000000000000ff"
      "ff",
      &data));
  ASSERT_FALSE(ABIDecode({"bytes"}, data));

  // Invalid bytes length (number too large)
  ASSERT_TRUE(PrefixedHexStringToBytes(
      "0x0000000000000000000000000000000000000000000000000000000000000020"
      "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff",
      &data));
  ASSERT_FALSE(ABIDecode({"bytes"}, data));
}

TEST(EthABIDecoderTest, ABIDecodeUnknownType) {
  std::vector<std::string> tx_params;
  std::vector<std::string> tx_args;

  std::vector<uint8_t> data;
  ASSERT_TRUE(PrefixedHexStringToBytes(
      "0x00000000000000000000000000000000000000000000000000000000000000ff",
      &data));
  auto decoded = ABIDecode({"supertype"}, data);
  ASSERT_NE(decoded, absl::nullopt);
  std::tie(tx_params, tx_args) = *decoded;

  ASSERT_EQ(tx_params.size(), 1UL);
  EXPECT_EQ(tx_params[0], "supertype");
  EXPECT_EQ(tx_args[0],
            "00000000000000000000000000000000000000000000000000000000000000ff");
}

TEST(EthABIDecoderTest, UniswapEncodedPathDecodeValid) {
  // Single-hop swap: WETH → STG
  absl::optional<std::vector<std::string>> path;
  path = UniswapEncodedPathDecode(
      "0xc02aaa39b223fe8d0a0e5c4f27ead9083c756cc2"  // WETH
      "002710"                                      // POOL FEE (10000)
      "af5191b0de278c7286d6c7cc6ab6bb8a73ba2cd6");  // STG
  ASSERT_NE(path, absl::nullopt);
  ASSERT_EQ(path->size(), 2UL);
  ASSERT_EQ(path->at(0), "0xc02aaa39b223fe8d0a0e5c4f27ead9083c756cc2");
  ASSERT_EQ(path->at(1), "0xaf5191b0de278c7286d6c7cc6ab6bb8a73ba2cd6");

  // Multi-hop swap: RSS3 → USDC → WETH
  path = UniswapEncodedPathDecode(
      "0xc98d64da73a6616c42117b582e832812e7b8d57f"  // RSS3
      "000bb8"                                      // POOL FEE (3000)
      "a0b86991c6218b36c1d19d4a2e9eb0ce3606eb48"    // USDC
      "0001f4"                                      // POOL FEE (500)
      "c02aaa39b223fe8d0a0e5c4f27ead9083c756cc2");  // WETH
  ASSERT_NE(path, absl::nullopt);
  ASSERT_EQ(path->size(), 3UL);
  ASSERT_EQ(path->at(0), "0xc98d64da73a6616c42117b582e832812e7b8d57f");
  ASSERT_EQ(path->at(1), "0xa0b86991c6218b36c1d19d4a2e9eb0ce3606eb48");
  ASSERT_EQ(path->at(2), "0xc02aaa39b223fe8d0a0e5c4f27ead9083c756cc2");
}

TEST(EthABIDecoderTest, UniswapEncodedPathDecodeInvalid) {
  // Empty string.
  ASSERT_FALSE(UniswapEncodedPathDecode(""));

  // Missing hops.
  ASSERT_FALSE(UniswapEncodedPathDecode("0x"));

  // Missing source hop.
  ASSERT_FALSE(UniswapEncodedPathDecode(
      "0xc02aaa39b223fe8d0a0e5c4f27ead9083c756cc2"  // WETH
      "002710"));                                   // POOL FEE

  // Missing destination hop.
  ASSERT_FALSE(UniswapEncodedPathDecode(
      "0x002710"                                     // POOL FEE
      "af5191b0de278c7286d6c7cc6ab6bb8a73ba2cd6"));  // STG

  // Missing POOL FEE
  ASSERT_FALSE(UniswapEncodedPathDecode(
      "0xc02aaa39b223fe8d0a0e5c4f27ead9083c756cc2"   // WETH
      "af5191b0de278c7286d6c7cc6ab6bb8a73ba2cd6"));  // STG

  // Extraneous data
  ASSERT_FALSE(UniswapEncodedPathDecode(
      "0xc02aaa39b223fe8d0a0e5c4f27ead9083c756cc2"  // WETH
      "002710"                                      // POOL FEE
      "af5191b0de278c7286d6c7cc6ab6bb8a73ba2cd6"    // STG
      "deadbeef"));                                 // Bogus data
}

TEST(EthABIDecoderTest, SuccessBytes) {
  std::vector<std::string> tx_params;
  std::vector<std::string> tx_args;
  std::vector<uint8_t> data;
  ASSERT_TRUE(PrefixedHexStringToBytes(
      // "0x0000000000000000000000000000000000000000000000000000000000000020" //
      // offset for length
      // "0000000000000000000000000000000000000000000000000000000000000001" //
      // actual length
      // "0000000000000000000000000000000000000000000000000000000000000020" //
      // offset of first result

      "0x0000000000000000000000000000000000000000000000000000000000000001"
      "0000000000000000000000000000000000000000000000000000000000000040"
      "0000000000000000000000000000000000000000000000000000000000000020"
      "0000000000000000000000000000000000000000000000000000000000000000",
      &data));
  auto decoded = ABIDecode({"bool", "bytes"}, data);
  ASSERT_NE(decoded, absl::nullopt);
  std::tie(tx_params, tx_args) = *decoded;
  ASSERT_EQ(tx_params.size(), 2UL);
  EXPECT_EQ(tx_params[0], "bool");
  EXPECT_EQ(tx_args[0], "true");
  EXPECT_EQ(tx_params[1], "bytes");
  EXPECT_EQ(
      tx_args[1],
      "0x0000000000000000000000000000000000000000000000000000000000000000");
}

TEST(EthABIDecoderTest, ABIDecodeBalanceScannerResult) {
  std::vector<std::string> tx_params;
  std::vector<std::string> tx_args;
  std::vector<uint8_t> data;
  absl::optional<std::vector<
      std::tuple<std::vector<std::string>, std::vector<std::string>>>>
      decoded;

  // 1 token contract address
  ASSERT_TRUE(PrefixedHexStringToBytes(
      "0x0000000000000000000000000000000000000000000000000000000000000020"
      "0000000000000000000000000000000000000000000000000000000000000001"
      "0000000000000000000000000000000000000000000000000000000000000020"
      "0000000000000000000000000000000000000000000000000000000000000001"
      "0000000000000000000000000000000000000000000000000000000000000040"
      "0000000000000000000000000000000000000000000000000000000000000020"
      "000000000000000000000000000000000000000000000006e83695ab1f893c00",
      &data));
  decoded = ABIDecodeBalanceScannerResult(data);
  ASSERT_TRUE(decoded);
  ASSERT_EQ(decoded->size(), 1UL);
  std::tie(tx_params, tx_args) = decoded->at(0);
  ASSERT_EQ(tx_params.size(), 2UL);
  EXPECT_EQ(tx_params[0], "bool");
  EXPECT_EQ(tx_args[0], "true");
  EXPECT_EQ(tx_params[1], "bytes");
  EXPECT_EQ(
      tx_args[1],
      "0x000000000000000000000000000000000000000000000006e83695ab1f893c00");

  // 3 token contract addresses
  ASSERT_TRUE(PrefixedHexStringToBytes(
      "0x0000000000000000000000000000000000000000000000000000000000000020"
      "0000000000000000000000000000000000000000000000000000000000000003"
      "0000000000000000000000000000000000000000000000000000000000000060"
      "00000000000000000000000000000000000000000000000000000000000000e0"
      "0000000000000000000000000000000000000000000000000000000000000160"
      "0000000000000000000000000000000000000000000000000000000000000001"
      "0000000000000000000000000000000000000000000000000000000000000040"
      "0000000000000000000000000000000000000000000000000000000000000020"
      "0000000000000000000000000000000000000000000000000000000000000000"
      "0000000000000000000000000000000000000000000000000000000000000001"
      "0000000000000000000000000000000000000000000000000000000000000040"
      "0000000000000000000000000000000000000000000000000000000000000020"
      "0000000000000000000000000000000000000000000000000000000000000000"
      "0000000000000000000000000000000000000000000000000000000000000001"
      "0000000000000000000000000000000000000000000000000000000000000040"
      "0000000000000000000000000000000000000000000000000000000000000020"
      "0000000000000000000000000000000000000000000000000000000000000000",
      &data));
  decoded = ABIDecodeBalanceScannerResult(data);
  ASSERT_TRUE(decoded.has_value());
  ASSERT_EQ(decoded->size(), 3UL);
  std::tie(tx_params, tx_args) = decoded->at(0);
  ASSERT_EQ(tx_params.size(), 2UL);
  EXPECT_EQ(tx_params[0], "bool");
  EXPECT_EQ(tx_args[0], "true");
  EXPECT_EQ(tx_params[1], "bytes");
  EXPECT_EQ(
      tx_args[1],
      "0x0000000000000000000000000000000000000000000000000000000000000000");
  std::tie(tx_params, tx_args) = decoded->at(1);
  ASSERT_EQ(tx_params.size(), 2UL);
  EXPECT_EQ(tx_params[0], "bool");
  EXPECT_EQ(tx_args[0], "true");
  EXPECT_EQ(tx_params[1], "bytes");
  EXPECT_EQ(
      tx_args[1],
      "0x0000000000000000000000000000000000000000000000000000000000000000");
  std::tie(tx_params, tx_args) = decoded->at(2);
  ASSERT_EQ(tx_params.size(), 2UL);
  EXPECT_EQ(tx_params[0], "bool");
  EXPECT_EQ(tx_args[0], "true");
  EXPECT_EQ(tx_params[1], "bytes");
}

}  // namespace brave_wallet
