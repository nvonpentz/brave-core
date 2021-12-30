// Copyright (c) 2021 The Brave Authors. All rights reserved.
// This Source Code Form is subject to the terms of the Mozilla Public
// License, v. 2.0. If a copy of the MPL was not distributed with this file,
// you can obtain one at http://mozilla.org/MPL/2.0/.

#ifndef BRAVE_BROWSER_SKUS_SDK_SERVICE_FACTORY_H_
#define BRAVE_BROWSER_SKUS_SDK_SERVICE_FACTORY_H_

#include "base/memory/singleton.h"
#include "brave/components/skus/browser/sdk_service.h"
#include "components/keyed_service/content/browser_context_keyed_service_factory.h"
#include "mojo/public/cpp/bindings/pending_remote.h"

class BraveVpnServiceFactory;

namespace skus {

class SdkServiceFactory : public BrowserContextKeyedServiceFactory {
 public:
  static mojo::PendingRemote<mojom::SdkService> GetForContext(
      content::BrowserContext* context);
  static void BindForContext(
      content::BrowserContext* context,
      mojo::PendingReceiver<skus::mojom::SdkService> receiver);
  static SdkServiceFactory* GetInstance();

  SdkServiceFactory(const SdkServiceFactory&) = delete;
  SdkServiceFactory& operator=(const SdkServiceFactory&) = delete;

 private:
  friend struct base::DefaultSingletonTraits<SdkServiceFactory>;
  friend BraveVpnServiceFactory;

  SdkServiceFactory();
  ~SdkServiceFactory() override;

  // Used by BraveVpnServiceFactory
  static SdkService* GetForContextPrivate(content::BrowserContext* context);

  // BrowserContextKeyedServiceFactory overrides:
  KeyedService* BuildServiceInstanceFor(
      content::BrowserContext* context) const override;
  void RegisterProfilePrefs(
      user_prefs::PrefRegistrySyncable* registry) override;
};

}  // namespace skus

#endif  // BRAVE_BROWSER_SKUS_SDK_SERVICE_FACTORY_H_
