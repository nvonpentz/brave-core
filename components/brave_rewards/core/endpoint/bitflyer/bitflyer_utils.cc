/* Copyright (c) 2021 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/brave_rewards/core/endpoint/bitflyer/bitflyer_utils.h"

#include "base/base64.h"
#include "base/strings/stringprintf.h"
#include "brave/components/brave_rewards/core/bitflyer/bitflyer_util.h"
#include "brave/components/brave_rewards/core/buildflags.h"
#include "brave/components/brave_rewards/core/ledger_impl.h"

namespace ledger {
namespace endpoint {
namespace bitflyer {

const char kUrlStaging[] = BUILDFLAG(BITFLYER_STAGING_URL);
const char kUrlProduction[] = "https://bitflyer.com";

std::string GetClientId() {
  return ::ledger::bitflyer::GetClientId();
}

std::string GetClientSecret() {
  return ::ledger::bitflyer::GetClientSecret();
}

std::vector<std::string> RequestAuthorization(const std::string& token) {
  std::vector<std::string> headers;

  if (!token.empty()) {
    headers.push_back("Authorization: Bearer " + token);
    return headers;
  }

  const std::string id = GetClientId();
  const std::string secret = GetClientSecret();

  std::string user;
  base::Base64Encode(base::StringPrintf("%s:%s", id.c_str(), secret.c_str()),
                     &user);

  headers.push_back("Authorization: Basic " + user);

  return headers;
}

std::string GetServerUrl(const std::string& path) {
  DCHECK(!path.empty());

  std::string url;
  if (ledger::_environment == mojom::Environment::PRODUCTION) {
    url = kUrlProduction;
  } else {
    url = kUrlStaging;
  }

  return url + path;
}

}  // namespace bitflyer
}  // namespace endpoint
}  // namespace ledger
