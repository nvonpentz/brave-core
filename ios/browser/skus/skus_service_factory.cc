/* Copyright (c) 2021 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "brave/ios/browser/skus/skus_service_factory.h"

#include "brave/components/skus/browser/skus_utils.h"
#include "brave/components/skus/browser/skus_service.h"
#include "components/keyed_service/core/keyed_service.h"
#include "components/keyed_service/ios/browser_state_dependency_manager.h"
#include "components/pref_registry/pref_registry_syncable.h"
#include "ios/chrome/browser/browser_state/browser_state_otr_helper.h"
#include "ios/chrome/browser/browser_state/chrome_browser_state.h"
#include "ios/web/public/browser_state.h"
#include "services/network/public/cpp/shared_url_loader_factory.h"

namespace skus {

// static
mojom::SkusService* SkusServiceFactory::GetForBrowserState(
    ChromeBrowserState* browser_state) {
  return static_cast<skus::SkusService*>(
      GetInstance()->GetServiceForBrowserState(browser_state, true));
}

// static
SkusServiceFactory* SkusServiceFactory::GetInstance() {
  return base::Singleton<SkusServiceFactory>::get();
}

SkusServiceFactory::SkusServiceFactory()
    : BrowserStateKeyedServiceFactory(
          "SkusSkusService",
          BrowserStateDependencyManager::GetInstance()) {}

SkusServiceFactory::~SkusServiceFactory() = default;

std::unique_ptr<KeyedService> SkusServiceFactory::BuildServiceInstanceFor(
    web::BrowserState* context) const {
  auto* browser_state = ChromeBrowserState::FromBrowserState(context);
  if (browser_state->IsOffTheRecord()) {
    return nullptr;
  }
  std::unique_ptr<SkusService> sku_service(new SkusService(
      browser_state->GetPrefs(), browser_state->GetSharedURLLoaderFactory()));
  return sku_service;
}

void SkusServiceFactory::RegisterBrowserStatePrefs(
    user_prefs::PrefRegistrySyncable* registry) {
  skus::RegisterProfilePrefs(registry);
}

bool SkusServiceFactory::ServiceIsNULLWhileTesting() const {
  return true;
}

}  // namespace skus
