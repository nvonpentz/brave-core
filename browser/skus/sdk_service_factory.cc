// Copyright (c) 2021 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// you can obtain one at http://mozilla.org/MPL/2.0/.

#include "brave/browser/skus/sdk_service_factory.h"

#include "brave/browser/profiles/profile_util.h"
#include "brave/components/skus/browser/pref_names.h"
#include "chrome/browser/profiles/profile.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/storage_partition.h"

namespace {

bool IsAllowedForContext(content::BrowserContext* context) {
  if (context && !brave::IsRegularProfile(context))
    return false;

  return true;
}

}  // namespace

namespace skus {

// static
SdkServiceFactory* SdkServiceFactory::GetInstance() {
  return base::Singleton<SdkServiceFactory>::get();
}

// static
mojo::PendingRemote<mojom::SdkService> SdkServiceFactory::GetForContext(
    content::BrowserContext* context) {
  if (!IsAllowedForContext(context)) {
    return mojo::PendingRemote<mojom::SdkService>();
  }
  return static_cast<skus::SdkService*>(
             GetInstance()->GetServiceForBrowserContext(context, true))
      ->MakeRemote();
}

// static
skus::SdkService* SdkServiceFactory::GetForContext_WRONG(
    content::BrowserContext* context) {
  if (!IsAllowedForContext(context)) {
    return nullptr;
  }
  return static_cast<skus::SdkService*>(
      GetInstance()->GetServiceForBrowserContext(context, true));
}

SdkServiceFactory::SdkServiceFactory()
    : BrowserContextKeyedServiceFactory(
          "SdkService",
          BrowserContextDependencyManager::GetInstance()) {}

SdkServiceFactory::~SdkServiceFactory() = default;

KeyedService* SdkServiceFactory::BuildServiceInstanceFor(
    content::BrowserContext* context) const {
  // Skus functionality not supported in private / Tor / guest windows
  if (!brave::IsRegularProfile(context)) {
    return nullptr;
  }

  return new skus::SdkService(
      Profile::FromBrowserContext(context)->GetPrefs(),
      context->GetDefaultStoragePartition()
          ->GetURLLoaderFactoryForBrowserProcess());
}

void SdkServiceFactory::RegisterProfilePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  registry->RegisterDictionaryPref(prefs::kSkusState);
  registry->RegisterBooleanPref(prefs::kSkusVPNHasCredential, false);
}

}  // namespace skus
