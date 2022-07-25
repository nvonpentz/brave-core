/* Copyright (c) 2022 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "brave/browser/brave_ads/notification_helper/notification_helper.h"

#include "base/bind.h"
#include "base/memory/singleton.h"
#include "brave/browser/brave_ads/notification_helper/notification_helper_impl.h"
#include "build/build_config.h"
#include "chrome/browser/browser_process.h"
#include "chrome/browser/notifications/notification_platform_bridge.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/common/chrome_features.h"
#include "chrome/common/pref_names.h"
#include "components/prefs/pref_service.h"

#if BUILDFLAG(IS_ANDROID)
#include "brave/browser/brave_ads/notification_helper/notification_helper_impl_android.h"
#endif  // BUILDFLAG(IS_ANDROID)

#if BUILDFLAG(IS_LINUX)
#include "brave/browser/brave_ads/notification_helper/notification_helper_impl_linux.h"
#endif  // BUILDFLAG(IS_LINUX)

#if BUILDFLAG(IS_MAC)
#include "brave/browser/brave_ads/notification_helper/notification_helper_impl_mac.h"
#endif  // BUILDFLAG(IS_MAC)

#if BUILDFLAG(IS_WIN)
#include "brave/browser/brave_ads/notification_helper/notification_helper_impl_win.h"
#include "chrome/browser/notifications/notification_platform_bridge_win.h"
#endif  // BUILDFLAG(IS_WIN)

namespace {

bool SystemNotificationsEnabled(Profile* profile) {
#if BUILDFLAG(ENABLE_SYSTEM_NOTIFICATIONS)
#if BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_ANDROID)
  return true;
#elif BUILDFLAG(IS_WIN)
  return NotificationPlatformBridgeWin::SystemNotificationEnabled();
#else
#if BUILDFLAG(IS_LINUX)
  if (profile) {
    // Prefs take precedence over flags.
    PrefService* prefs = profile->GetPrefs();
    if (!prefs->GetBoolean(prefs::kAllowSystemNotifications)) {
      return false;
    }
  }
#endif  // BUILDFLAG(IS_LINUX)
  return base::FeatureList::IsEnabled(features::kNativeNotifications) &&
         base::FeatureList::IsEnabled(features::kSystemNotifications);
#endif  // BUILDFLAG(IS_CHROMEOS) || BUILDFLAG(IS_ANDROID)
#else
  return false;
#endif  // BUILDFLAG(ENABLE_SYSTEM_NOTIFICATIONS)
}

NotificationPlatformBridge* GetSystemNotificationPlatformBridge(
    Profile* profile) {
  if (SystemNotificationsEnabled(profile)) {
    return g_browser_process->notification_platform_bridge();
  }

  // The platform does not support, or has not enabled, system notifications.
  return nullptr;
}

}  // namespace

namespace brave_ads {

NotificationHelper::NotificationHelper() {
#if BUILDFLAG(IS_ANDROID)
  impl_.reset(new NotificationHelperImplAndroid());
#elif BUILDFLAG(IS_LINUX)
  impl_.reset(new NotificationHelperImplLinux());
#elif BUILDFLAG(IS_MAC)
  impl_.reset(new NotificationHelperImplMac());
#elif BUILDFLAG(IS_WIN)
  impl_.reset(new NotificationHelperImplWin());
#else
  // Default notification helper for unsupported platforms
  impl_.reset(new NotificationHelperImpl());
#endif  // BUILDFLAG(IS_ANDROID)
}

NotificationHelper::~NotificationHelper() = default;

// static
NotificationHelper* NotificationHelper::GetInstance() {
  return base::Singleton<NotificationHelper>::get();
}

void NotificationHelper::InitForProfile(Profile* profile) {
  NotificationPlatformBridge* system_bridge =
      GetSystemNotificationPlatformBridge(profile);
  if (!system_bridge) {
    system_notifications_supported_ = false;
    return;
  }

  system_bridge->SetReadyCallback(base::BindOnce(
      &NotificationHelper::OnSystemNotificationPlatformBridgeReady,
      weak_factory_.GetWeakPtr()));
}

bool NotificationHelper::CanShowNativeNotifications() {
  return impl_->CanShowNativeNotifications();
}

bool NotificationHelper::CanShowNativeNotificationsWhileBrowserIsBackgrounded()
    const {
  return impl_->CanShowNativeNotificationsWhileBrowserIsBackgrounded();
}

bool NotificationHelper::ShowOnboardingNotification() {
  return impl_->ShowOnboardingNotification();
}

bool NotificationHelper::SystemNotificationsSupported() const {
  return system_notifications_supported_;
}

void NotificationHelper::OnSystemNotificationPlatformBridgeReady(bool success) {
  system_notifications_supported_ = success;
}

}  // namespace brave_ads
