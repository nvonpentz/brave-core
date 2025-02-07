/* Copyright (c) 2020 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_COMPONENTS_BRAVE_REWARDS_CORE_DATABASE_DATABASE_PUBLISHER_INFO_H_
#define BRAVE_COMPONENTS_BRAVE_REWARDS_CORE_DATABASE_DATABASE_PUBLISHER_INFO_H_

#include <string>

#include "brave/components/brave_rewards/core/database/database_table.h"

namespace ledger {
namespace database {

class DatabasePublisherInfo : public DatabaseTable {
 public:
  explicit DatabasePublisherInfo(LedgerImpl& ledger);
  ~DatabasePublisherInfo() override;

  void InsertOrUpdate(mojom::PublisherInfoPtr info,
                      ledger::LegacyResultCallback callback);

  void GetRecord(const std::string& publisher_key,
                 ledger::GetPublisherInfoCallback callback);

  void GetPanelRecord(mojom::ActivityInfoFilterPtr filter,
                      ledger::GetPublisherPanelInfoCallback callback);

  void RestorePublishers(ledger::ResultCallback callback);

  void GetExcludedList(ledger::GetExcludedListCallback callback);

 private:
  void OnGetRecord(mojom::DBCommandResponsePtr response,
                   ledger::GetPublisherInfoCallback callback);

  void OnGetPanelRecord(mojom::DBCommandResponsePtr response,
                        ledger::GetPublisherPanelInfoCallback callback);

  void OnRestorePublishers(ledger::ResultCallback callback,
                           mojom::DBCommandResponsePtr response);

  void OnGetExcludedList(mojom::DBCommandResponsePtr response,
                         ledger::GetExcludedListCallback callback);
};

}  // namespace database
}  // namespace ledger

#endif  // BRAVE_COMPONENTS_BRAVE_REWARDS_CORE_DATABASE_DATABASE_PUBLISHER_INFO_H_
