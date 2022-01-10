/* Copyright (c) 2022 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this
 * file, You can obtain one at http://mozilla.org/MPL/2.0/. */

#ifndef BRAVE_VENDOR_BAT_NATIVE_LEDGER_SRC_BAT_LEDGER_INTERNAL_DATABASE_MIGRATION_MIGRATION_V34_H_
#define BRAVE_VENDOR_BAT_NATIVE_LEDGER_SRC_BAT_LEDGER_INTERNAL_DATABASE_MIGRATION_MIGRATION_V34_H_

namespace ledger {
namespace database {
namespace migration {

// Migration 34 attempts to restart failed ACs where BAT was transferred from a
// user's external wallet but the SKU transaction was never completed.
const char v34[] = R"sql(
  UPDATE contribution_info SET step = 4, retry_count = 0
  WHERE contribution_id IN (
    SELECT ci.contribution_id
    FROM contribution_info ci
    INNER JOIN contribution_info_publishers cip
      ON cip.contribution_id = ci.contribution_id
    INNER JOIN sku_order so
      ON so.contribution_id = ci.contribution_id
    WHERE ci.step = -7 AND ci.type = 2 AND so.status = 2
    GROUP BY ci.contribution_id
    HAVING SUM(cip.contributed_amount) = 0)
)sql";

}  // namespace migration
}  // namespace database
}  // namespace ledger

#endif  // BRAVE_VENDOR_BAT_NATIVE_LEDGER_SRC_BAT_LEDGER_INTERNAL_DATABASE_MIGRATION_MIGRATION_V34_H_
