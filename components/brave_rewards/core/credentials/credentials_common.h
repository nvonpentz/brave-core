/* Copyright (c) 2020 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_BRAVE_REWARDS_CORE_CREDENTIALS_CREDENTIALS_COMMON_H_
#define BRAVE_COMPONENTS_BRAVE_REWARDS_CORE_CREDENTIALS_CREDENTIALS_COMMON_H_

#include <stdint.h>

#include <map>
#include <string>
#include <vector>

#include "base/memory/raw_ref.h"
#include "brave/components/brave_rewards/core/credentials/credentials.h"
#include "brave/components/brave_rewards/core/ledger_callbacks.h"

namespace ledger {
class LedgerImpl;

namespace credential {

class CredentialsCommon {
 public:
  explicit CredentialsCommon(LedgerImpl& ledger);
  ~CredentialsCommon();

  void GetBlindedCreds(const CredentialsTrigger& trigger,
                       ledger::ResultCallback callback);

  void SaveUnblindedCreds(
      uint64_t expires_at,
      double token_value,
      const mojom::CredsBatch& creds,
      const std::vector<std::string>& unblinded_encoded_creds,
      const CredentialsTrigger& trigger,
      ledger::ResultCallback callback);

 private:
  void BlindedCredsSaved(ledger::ResultCallback callback, mojom::Result result);

  void OnSaveUnblindedCreds(ledger::ResultCallback callback,
                            const CredentialsTrigger& trigger,
                            mojom::Result result);

  const raw_ref<LedgerImpl> ledger_;
};

}  // namespace credential
}  // namespace ledger

#endif  // BRAVE_COMPONENTS_BRAVE_REWARDS_CORE_CREDENTIALS_CREDENTIALS_COMMON_H_
