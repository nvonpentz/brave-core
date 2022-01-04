/* Copyright (c) 2021 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "brave/components/brave_wallet/browser/eth_block_tracker.h"

#include <utility>

#include "base/bind.h"
#include "base/logging.h"
#include "brave/components/brave_wallet/browser/eth_json_rpc_controller.h"

namespace brave_wallet {

EthBlockTracker::EthBlockTracker(EthJsonRpcController* rpc_controller)
    : rpc_controller_(rpc_controller), weak_factory_(this) {
  DCHECK(rpc_controller_);
}
EthBlockTracker::~EthBlockTracker() = default;

void EthBlockTracker::Start(base::TimeDelta interval) {
  timer_.Start(FROM_HERE, interval,
               base::BindRepeating(&EthBlockTracker::GetBlockNumber,
                                   weak_factory_.GetWeakPtr()));
}

void EthBlockTracker::Stop() {
  timer_.Stop();
}

bool EthBlockTracker::IsRunning() const {
  return timer_.IsRunning();
}

void EthBlockTracker::AddObserver(EthBlockTracker::Observer* observer) {
  observers_.AddObserver(observer);
}

void EthBlockTracker::RemoveObserver(EthBlockTracker::Observer* observer) {
  observers_.RemoveObserver(observer);
}

void EthBlockTracker::CheckForLatestBlock(
    base::OnceCallback<void(uint256_t block_num,
                            mojom::ProviderError error,
                            const std::string& error_message)> callback) {
  SendGetBlockNumber(std::move(callback));
}

void EthBlockTracker::SendGetBlockNumber(
    base::OnceCallback<void(uint256_t block_num,
                            mojom::ProviderError error,
                            const std::string& error_message)> callback) {
  rpc_controller_->GetBlockNumber(std::move(callback));
}

void EthBlockTracker::GetBlockNumber() {
  rpc_controller_->GetBlockNumber(base::BindOnce(
      &EthBlockTracker::OnGetBlockNumber, weak_factory_.GetWeakPtr()));
}

void EthBlockTracker::OnGetBlockNumber(uint256_t block_num,
                                       mojom::ProviderError error,
                                       const std::string& error_message) {
  if (error == mojom::ProviderError::kSuccess) {
    if (current_block_ != block_num) {
      current_block_ = block_num;
      for (auto& observer : observers_)
        observer.OnNewBlock(block_num);
    }
    for (auto& observer : observers_)
      observer.OnLatestBlock(block_num);

  } else {
    LOG(ERROR) << "GetBlockNumber failed";
  }
}

}  // namespace brave_wallet
