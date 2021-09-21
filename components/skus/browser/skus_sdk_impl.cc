// Copyright (c) 2021 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// you can obtain one at http://mozilla.org/MPL/2.0/.

#include "brave/components/skus/browser/skus_sdk_impl.h"

#include <vector>

#include "brave/components/skus/browser/br-rs/brave-rewards-cxx/src/wrapper.hpp"
#include "brave/components/skus/browser/pref_names.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "components/prefs/pref_service.h"

using namespace std;
using namespace rust::cxxbridge1;
using namespace brave_rewards;

namespace brave_rewards {

// TODO: hack code - remove me
// rust::String's std::string operator gave linker errors :(
// .c_str() not usable because function itself isn't const
// .data is not terminating string
std::string ruststring_2_stdstring(rust::String in) {
  std::string out = "";
  rust::String::iterator it = in.begin();
  while (it != in.end()) {
    out += (char)*it;
    it++;
  }
  return out;
}
std::string ruststr_2_stdstring(rust::cxxbridge1::Str in) {
  std::string out = "";
  rust::cxxbridge1::Str::iterator it = in.begin();
  while (it != in.end()) {
    out += (char)*it;
    it++;
  }
  return out;
}
std::string RewardsResult_2_stdstring(RewardsResult in) {
  switch (in) {
    case RewardsResult::Ok:
      return "OK";
    case RewardsResult::PointDecompressionError:
      return "An error occured when converting from a `CompressedRistretto` to "
             "a `RistrettoPoint`";
    case RewardsResult::ScalarFormatError:
      return "An error occured when interpretting bytes as a scalar";
    case RewardsResult::BytesLengthError:
      return "An error in the length of bytes handed to a constructor.";
    case RewardsResult::VerifyError:
      return "Verification failed";
    case RewardsResult::LengthMismatchError:
      return "Inputs differed in length";
    case RewardsResult::DecodingError:
      return "Decoding failed";
  }
  return "NULL";
}

void shim_executeRequest(
    const brave_rewards::HttpRequest& req,
    rust::cxxbridge1::Fn<void(rust::cxxbridge1::Box<brave_rewards::HttpRoundtripContext>, brave_rewards::HttpResponse)> callback,
    rust::cxxbridge1::Box<brave_rewards::HttpRoundtripContext> ctx
  ) {
  LOG(ERROR) << "shim_executeRequest\n"
             << "url=" << ruststring_2_stdstring(req.url)
             << "\nmethod=" << ruststring_2_stdstring(req.method)
             << "\nheaders=";
  for (size_t i = 0; i < req.headers.size(); i++) {
    LOG(ERROR) << i << ") \"" << ruststring_2_stdstring(req.headers[i]) << "\"";
  }
  std::string body2 = "";
  for (size_t i = 0; i < req.body.size(); i++) {
    body2 += (char)req.body[i];
  }
  LOG(ERROR) << "body=" << body2;

  // TODO: call out to req.url w/ headers
  // using SimpleUrlLoader

  // START - demo code from examples/main.cpp
  std::string body = "{\"id\":\"b788a168-1136-411f-9546-43a372a2e3ed\",\"createdAt\":\"2021-08-17T21:35:48.658623Z\",\"currency\":\"USD\",\"updatedAt\":\"2021-08-17T21:36:06.174288Z\",\"totalPrice\":\"7\",\"merchantId\":\"brave.com\",\"location\":\"talk.brave.software\",\"status\":\"paid\",\"items\":[{\"id\":\"eac1b09f-2228-4f91-a970-a22b229bb994\",\"orderId\":\"b788a168-1136-411f-9546-43a372a2e3ed\",\"sku\":\"brave-talk-premium\",\"createdAt\":\"2021-08-17T21:35:48.658623Z\",\"updatedAt\":\"2021-08-17T21:35:48.658623Z\",\"currency\":\"USD\",\"quantity\":1,\"price\":\"7\",\"subtotal\":\"7\",\"location\":\"talk.brave.software\",\"description\":\"Premium access to Brave Talk\",\"credentialType\":\"time-limited\",\"validFor\":null,\"metadata\":{\"stripe_cancel_uri\":\"https://account.brave.software/plans/?intent=checkout\",\"stripe_item_id\":\"price_1J84oMHof20bphG6NBAT2vor\",\"stripe_product_id\":\"prod_Jlc224hFvAMvEp\",\"stripe_success_uri\":\"https://account.brave.software/account/?intent=provision\"}}],\"allowedPaymentMethods\":[\"stripe\"],\"metadata\":{\"stripeSubscriptionId\":\"sub_K3hLyRFkjj3mYs\"},\"lastPaidAt\":\"2021-08-17T21:36:06.174938Z\",\"expiresAt\":\"2021-09-17T08:05:09.176138Z\",\"validFor\":2629743001200000}";
  std::vector<uint8_t> body_bytes (body.begin(), body.end());

  brave_rewards::HttpResponse resp = {
    RewardsResult::Ok,
    200,
    {"foo:bar"},
    body_bytes,
  };

  callback(std::move(ctx), resp);
  // END - demo code from examples/main.cpp
}

// static
void SkusSdkImpl::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterDictionaryPref(prefs::kSkusDictionary);
  registry->RegisterStringPref(prefs::kSkusVPNCredential, "");
}

SkusSdkImpl::SkusSdkImpl(PrefService* prefs) {}
// TODO: re-implement when setting preferences
// SkusSdkImpl::SkusSdkImpl(PrefService* prefs) : prefs_(prefs) {}

void on_refresh_order(RefreshOrderCallbackState* callback_state,
                      RewardsResult result,
                      rust::cxxbridge1::Str order) {
  std::string order_str = ruststr_2_stdstring(order);

  LOG(ERROR) << "on_refresh_order\nresult=" << RewardsResult_2_stdstring(result)
             << "\norder=" << order_str;

  if (callback_state->cb) {
    std::move(callback_state->cb).Run(order_str);
  }
  delete callback_state;
}

void SkusSdkImpl::RefreshOrder(const std::string& order_id,
                               RefreshOrderCallback callback) {
  Box<CppSDK> sdk = initialize_sdk("local");
  LOG(ERROR) << "rewards sdk initialized";

  std::unique_ptr<RefreshOrderCallbackState> cbs(new RefreshOrderCallbackState);
  cbs->cb = std::move(callback);

  sdk->refresh_order(on_refresh_order, std::move(cbs), order_id.c_str());
}

void SkusSdkImpl::FetchOrderCredentials(const std::string& order_id) {
  // TODO: fill me in
}

} // namespace brave_rewards
