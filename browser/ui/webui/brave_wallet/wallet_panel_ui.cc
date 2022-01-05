/* Copyright (c) 2021 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include "brave/browser/ui/webui/brave_wallet/wallet_panel_ui.h"

#include <string>
#include <utility>

#include "base/bind.h"
#include "brave/browser/brave_wallet/asset_ratio_service_factory.h"
#include "brave/browser/brave_wallet/brave_wallet_service_factory.h"
#include "brave/browser/brave_wallet/eth_tx_service_factory.h"
#include "brave/browser/brave_wallet/json_rpc_service_factory.h"
#include "brave/browser/brave_wallet/keyring_service_factory.h"
#include "brave/browser/brave_wallet/swap_service_factory.h"
#include "brave/browser/ui/webui/brave_wallet/wallet_common_ui.h"
#include "brave/common/webui_url_constants.h"
#include "brave/components/brave_wallet/browser/asset_ratio_service.h"
#include "brave/components/brave_wallet/browser/brave_wallet_constants.h"
#include "brave/components/brave_wallet/browser/brave_wallet_service.h"
#include "brave/components/brave_wallet/browser/erc_token_registry.h"
#include "brave/components/brave_wallet/browser/eth_tx_service.h"
#include "brave/components/brave_wallet/browser/json_rpc_service.h"
#include "brave/components/brave_wallet/browser/keyring_service.h"
#include "brave/components/brave_wallet/browser/swap_service.h"
#include "brave/components/brave_wallet_panel/resources/grit/brave_wallet_panel_generated_map.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/ui/browser.h"
#include "chrome/browser/ui/browser_list.h"
#include "chrome/browser/ui/tabs/tab_strip_model.h"
#include "chrome/browser/ui/webui/webui_util.h"
#include "components/grit/brave_components_resources.h"
#include "components/grit/brave_components_strings.h"
#include "components/sessions/content/session_tab_helper.h"
#include "content/public/browser/web_contents.h"
#include "content/public/browser/web_ui.h"
#include "content/public/browser/web_ui_data_source.h"
#include "content/public/common/url_constants.h"
#include "ui/base/accelerators/accelerator.h"
#include "ui/base/webui/web_ui_util.h"

WalletPanelUI::WalletPanelUI(content::WebUI* web_ui)
    : ui::MojoBubbleWebUIController(web_ui,
                                    true /* Needed for webui browser tests */) {
  content::WebUIDataSource* source =
      content::WebUIDataSource::Create(kWalletPanelHost);
  web_ui->AddRequestableScheme(content::kChromeUIUntrustedScheme);
  source->AddLocalizedStrings(brave_wallet::kLocalizedStrings);
  webui::SetupWebUIDataSource(source,
                              base::make_span(kBraveWalletPanelGenerated,
                                              kBraveWalletPanelGeneratedSize),
                              IDR_WALLET_PANEL_HTML);
  source->OverrideContentSecurityPolicy(
      network::mojom::CSPDirectiveName::FrameSrc,
      std::string("frame-src ") + kUntrustedTrezorURL + ";");
  source->AddString("braveWalletTrezorBridgeUrl", kUntrustedTrezorURL);
  auto* profile = Profile::FromWebUI(web_ui);
  content::WebUIDataSource::Add(profile, source);
  brave_wallet::AddERCTokenImageSource(profile);
}

WalletPanelUI::~WalletPanelUI() = default;

WEB_UI_CONTROLLER_TYPE_IMPL(WalletPanelUI)

void WalletPanelUI::BindInterface(
    mojo::PendingReceiver<brave_wallet::mojom::PanelHandlerFactory> receiver) {
  panel_factory_receiver_.reset();
  panel_factory_receiver_.Bind(std::move(receiver));
}

void WalletPanelUI::SetDeactivationCallback(
    base::RepeatingCallback<void(bool)> deactivation_callback) {
  deactivation_callback_ = std::move(deactivation_callback);
}

void WalletPanelUI::CreatePanelHandler(
    mojo::PendingRemote<brave_wallet::mojom::Page> page,
    mojo::PendingReceiver<brave_wallet::mojom::PanelHandler> panel_receiver,
    mojo::PendingReceiver<brave_wallet::mojom::WalletHandler> wallet_receiver,
    mojo::PendingReceiver<brave_wallet::mojom::JsonRpcService>
        json_rpc_service_receiver,
    mojo::PendingReceiver<brave_wallet::mojom::SwapService>
        swap_service_receiver,
    mojo::PendingReceiver<brave_wallet::mojom::AssetRatioService>
        asset_ratio_service_receiver,
    mojo::PendingReceiver<brave_wallet::mojom::KeyringService>
        keyring_service_receiver,
    mojo::PendingReceiver<brave_wallet::mojom::ERCTokenRegistry>
        erc_token_registry_receiver,
    mojo::PendingReceiver<brave_wallet::mojom::EthTxService>
        eth_tx_service_receiver,
    mojo::PendingReceiver<brave_wallet::mojom::BraveWalletService>
        brave_wallet_service_receiver) {
  DCHECK(page);
  auto* profile = Profile::FromWebUI(web_ui());
  DCHECK(profile);

  panel_handler_ = std::make_unique<WalletPanelHandler>(
      std::move(panel_receiver), this,
      base::BindRepeating(&brave_wallet::GetActiveWebContents),
      std::move(deactivation_callback_));
  wallet_handler_ =
      std::make_unique<WalletHandler>(std::move(wallet_receiver), profile);

  auto* json_rpc_service =
      brave_wallet::JsonRpcServiceFactory::GetServiceForContext(profile);
  if (json_rpc_service) {
    json_rpc_service->Bind(std::move(json_rpc_service_receiver));
  }

  auto* swap_service =
      brave_wallet::SwapServiceFactory::GetServiceForContext(profile);
  if (swap_service) {
    swap_service->Bind(std::move(swap_service_receiver));
  }

  auto* asset_ratio_service =
      brave_wallet::AssetRatioServiceFactory::GetServiceForContext(profile);
  if (asset_ratio_service) {
    asset_ratio_service->Bind(std::move(asset_ratio_service_receiver));
  }

  auto* keyring_service =
      brave_wallet::KeyringServiceFactory::GetServiceForContext(profile);
  if (keyring_service) {
    keyring_service->Bind(std::move(keyring_service_receiver));
  }

  auto* erc_token_registry = brave_wallet::ERCTokenRegistry::GetInstance();
  if (erc_token_registry) {
    erc_token_registry->Bind(std::move(erc_token_registry_receiver));
  }

  auto* eth_tx_service =
      brave_wallet::EthTxServiceFactory::GetServiceForContext(profile);
  if (eth_tx_service) {
    eth_tx_service->Bind(std::move(eth_tx_service_receiver));
  }

  auto* brave_wallet_service =
      brave_wallet::BraveWalletServiceFactory::GetServiceForContext(profile);
  if (brave_wallet_service) {
    brave_wallet_service->Bind(std::move(brave_wallet_service_receiver));
  }
}
