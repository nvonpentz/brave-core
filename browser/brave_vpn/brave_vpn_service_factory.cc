/* Copyright (c) 2021 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "brave/browser/brave_vpn/brave_vpn_service_factory.h"

#include "base/feature_list.h"
#include "brave/browser/skus/skus_service_factory.h"
#include "brave/components/skus/common/features.h"
#include "chrome/browser/profiles/incognito_helpers.h"
#include "chrome/browser/profiles/profile.h"
#include "components/keyed_service/content/browser_context_dependency_manager.h"
#include "components/user_prefs/user_prefs.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/storage_partition.h"

// TODO(bsclifton) or TODO(shong):
// We should be able to consolidate this integration into one implementation
// which we can share between Android and Desktop.
//
// As seen below, Desktop returns BraveVpnServiceDesktop and Android
// returns BraveVpnService.
//
// See https://github.com/brave/brave-browser/issues/20374 for more info.
#if defined(OS_WIN) || defined(OS_MAC)
#include "brave/components/brave_vpn/brave_vpn_service_desktop.h"
#include "brave/components/brave_vpn/brave_vpn_utils.h"
#elif defined(OS_ANDROID)
#include "brave/components/brave_vpn/brave_vpn_service.h"
#endif

// static
BraveVpnServiceFactory* BraveVpnServiceFactory::GetInstance() {
  return base::Singleton<BraveVpnServiceFactory>::get();
}

// static
#if defined(OS_WIN) || defined(OS_MAC)
BraveVpnServiceDesktop* BraveVpnServiceFactory::GetForProfile(
    Profile* profile) {
  if (!brave_vpn::IsBraveVPNEnabled())
    return nullptr;

  return static_cast<BraveVpnServiceDesktop*>(
      GetInstance()->GetServiceForBrowserContext(profile, true));
}
#elif defined(OS_ANDROID)
BraveVpnService* BraveVpnServiceFactory::GetForProfile(Profile* profile) {
  return static_cast<BraveVpnService*>(
      GetInstance()->GetServiceForBrowserContext(profile, true));
}
#endif

// TODO(bsclifton) or TODO(shong):
// BraveVpnServiceDesktop is currently only used on Desktop,
// which is why there are only OS guards for Windows and macOS.
// Consolidating the Android/Desktop behaviors is captured with:
// https://github.com/brave/brave-browser/issues/20374
BraveVpnServiceFactory::BraveVpnServiceFactory()
    : BrowserContextKeyedServiceFactory(
          "BraveVpnService",
          BrowserContextDependencyManager::GetInstance()) {
#if defined(OS_WIN) || defined(OS_MAC)
  DependsOn(skus::SkusServiceFactory::GetInstance());
#endif
}

BraveVpnServiceFactory::~BraveVpnServiceFactory() = default;

KeyedService* BraveVpnServiceFactory::BuildServiceInstanceFor(
    content::BrowserContext* context) const {
  auto* default_storage_partition = context->GetDefaultStoragePartition();
  auto shared_url_loader_factory =
      default_storage_partition->GetURLLoaderFactoryForBrowserProcess();

// TODO(bsclifton) or TODO(shong):
// see same notes above
#if defined(OS_WIN) || defined(OS_MAC)
  DCHECK(base::FeatureList::IsEnabled(skus::features::kSkusFeature));
  mojo::PendingRemote<skus::mojom::SkusService> pending =
      skus::SkusServiceFactory::GetForContext(context);
  return new BraveVpnServiceDesktop(
      shared_url_loader_factory, user_prefs::UserPrefs::Get(context),
      std::move(pending));
#elif defined(OS_ANDROID)
  return new BraveVpnService(shared_url_loader_factory);
#endif
}

content::BrowserContext* BraveVpnServiceFactory::GetBrowserContextToUse(
    content::BrowserContext* context) const {
  return chrome::GetBrowserContextRedirectedInIncognito(context);
}
