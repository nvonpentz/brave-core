/* Copyright (c) 2020 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at http://mozilla.org/MPL/2.0/. */

#include <memory>
#include <string>

#include "base/containers/flat_map.h"
#include "base/memory/raw_ptr.h"
#include "base/strings/stringprintf.h"
#include "base/test/bind.h"
#include "base/threading/platform_thread.h"
#include "brave/browser/brave_rewards/rewards_service_factory.h"
#include "brave/components/brave_rewards/browser/rewards_service_impl.h"
#include "brave/components/brave_rewards/browser/test/common/rewards_browsertest_context_helper.h"
#include "brave/components/brave_rewards/browser/test/common/rewards_browsertest_context_util.h"
#include "brave/components/brave_rewards/browser/test/common/rewards_browsertest_contribution.h"
#include "brave/components/brave_rewards/browser/test/common/rewards_browsertest_network_util.h"
#include "brave/components/brave_rewards/browser/test/common/rewards_browsertest_promotion.h"
#include "brave/components/brave_rewards/browser/test/common/rewards_browsertest_response.h"
#include "brave/components/brave_rewards/browser/test/common/rewards_browsertest_util.h"
#include "brave/components/brave_rewards/common/pref_names.h"
#include "brave/components/constants/brave_paths.h"
#include "chrome/test/base/in_process_browser_test.h"
#include "chrome/test/base/testing_profile.h"
#include "chrome/test/base/ui_test_utils.h"
#include "components/network_session_configurator/common/network_switches.h"
#include "components/prefs/pref_service.h"
#include "content/public/test/browser_test.h"
#include "content/public/test/browser_test_utils.h"
#include "net/dns/mock_host_resolver.h"

// npm run test -- brave_browser_tests --filter=RewardsContributionBrowserTest.*

namespace rewards_browsertest {

class RewardsContributionBrowserTest : public InProcessBrowserTest {
 public:
  RewardsContributionBrowserTest() {
    contribution_ = std::make_unique<RewardsBrowserTestContribution>();
    promotion_ = std::make_unique<RewardsBrowserTestPromotion>();
    response_ = std::make_unique<RewardsBrowserTestResponse>();
  }

  void SetUpOnMainThread() override {
    InProcessBrowserTest::SetUpOnMainThread();

    context_helper_ =
        std::make_unique<RewardsBrowserTestContextHelper>(browser());

    // HTTP resolver
    host_resolver()->AddRule("*", "127.0.0.1");
    https_server_ = std::make_unique<net::EmbeddedTestServer>(
        net::test_server::EmbeddedTestServer::TYPE_HTTPS);
    https_server_->SetSSLConfig(net::EmbeddedTestServer::CERT_OK);
    https_server_->RegisterRequestHandler(
        base::BindRepeating(&rewards_browsertest_util::HandleRequest));
    ASSERT_TRUE(https_server_->Start());

    // Rewards service
    brave::RegisterPathProvider();
    auto* profile = browser()->profile();
    rewards_service_ = static_cast<brave_rewards::RewardsServiceImpl*>(
        brave_rewards::RewardsServiceFactory::GetForProfile(profile));

    // Response mock
    base::ScopedAllowBlockingForTesting allow_blocking;
    response_->LoadMocks();
    rewards_service_->ForTestingSetTestResponseCallback(
        base::BindRepeating(
            &RewardsContributionBrowserTest::GetTestResponse,
            base::Unretained(this)));
    rewards_service_->SetLedgerEnvForTesting();

    // Other
    promotion_->Initialize(browser(), rewards_service_);
    contribution_->Initialize(browser(), rewards_service_);

    rewards_browsertest_util::SetOnboardingBypassed(browser());
  }

  void TearDown() override {
    InProcessBrowserTest::TearDown();
  }

  void SetUpCommandLine(base::CommandLine* command_line) override {
    // HTTPS server only serves a valid cert for localhost, so this is needed
    // to load pages from other hosts without an error
    command_line->AppendSwitch(switches::kIgnoreCertificateErrors);
  }

  void GetTestResponse(
      const std::string& url,
      int32_t method,
      int* response_status_code,
      std::string* response,
      base::flat_map<std::string, std::string>* headers) {
    response_->SetExternalBalance(contribution_->GetExternalBalance());
    response_->Get(
        url,
        method,
        response_status_code,
        response);
  }

  content::WebContents* contents() {
    return browser()->tab_strip_model()->GetActiveWebContents();
  }

  std::string ExpectedTipSummaryAmountString() {
    // The tip summary page formats 2.4999 as 2.4, so we do the same here.
    double truncated_amount =
        floor(contribution_->GetReconcileTipTotal() * 10) / 10;
    return base::StringPrintf("%.2f BAT", -truncated_amount);
  }

  void RefreshPublisherListUsingRewardsPopup() {
    rewards_browsertest_util::WaitForElementThenClick(
        context_helper_->OpenRewardsPopup().get(),
        "[data-test-id=refresh-publisher-button]");
  }

  raw_ptr<brave_rewards::RewardsServiceImpl> rewards_service_ = nullptr;
  std::unique_ptr<net::EmbeddedTestServer> https_server_;
  std::unique_ptr<RewardsBrowserTestContribution> contribution_;
  std::unique_ptr<RewardsBrowserTestPromotion> promotion_;
  std::unique_ptr<RewardsBrowserTestResponse> response_;
  std::unique_ptr<RewardsBrowserTestContextHelper> context_helper_;
};

IN_PROC_BROWSER_TEST_F(RewardsContributionBrowserTest, AutoContribution) {
  rewards_browsertest_util::CreateRewardsWallet(rewards_service_);
  rewards_service_->SetAutoContributeEnabled(true);
  context_helper_->LoadRewardsPage();
  contribution_->AddBalance(promotion_->ClaimPromotionViaCode());

  context_helper_->VisitPublisher(
      rewards_browsertest_util::GetUrl(https_server_.get(), "duckduckgo.com"),
      true);

  rewards_service_->StartContributionsForTesting();

  contribution_->WaitForACReconcileCompleted();
  ASSERT_EQ(contribution_->GetACStatus(), ledger::mojom::Result::LEDGER_OK);

  contribution_->IsBalanceCorrect();

  rewards_browsertest_util::WaitForElementToContain(
      contents(), "[data-test-id=rewards-summary-ac]", "-20.00 BAT");
}

// TODO(https://github.com/brave/brave-browser/issues/29632): Test flaky on
// master for the mac build.
#if BUILDFLAG(IS_MAC)
#define MAYBE_AutoContributionUnconnected DISABLED_AutoContributionUnconnected
#else
#define MAYBE_AutoContributionUnconnected AutoContributionUnconnected
#endif  // BUILDFLAG(IS_MAC)
IN_PROC_BROWSER_TEST_F(RewardsContributionBrowserTest,
                       MAYBE_AutoContributionUnconnected) {
  // Set kEnabled to false before calling CreateRewardsWallet to ensure that
  // prefs are configured to reflect an unconnected user
  auto* pref_service = browser()->profile()->GetPrefs();
  pref_service->SetBoolean(brave_rewards::prefs::kEnabled, false);
  rewards_browsertest_util::CreateRewardsWallet(rewards_service_);

  // Visit publisher (this opens a new tab at index 1)
  rewards_browsertest_util::NavigateToPublisherPage(
      browser(), https_server_.get(), "duckduckgo.com");

  // The minimum publisher duration when testing is 1 second (and the
  // granularity is seconds), so wait for just over 2 seconds to elapse
  base::PlatformThread::Sleep(base::Milliseconds(2100));

  // Switch to original tab to trigger saving publisher activity
  browser()->tab_strip_model()->ActivateTabAt(0);

  rewards_service_->StartContributionsForTesting();

  // Switch back to publisher tab and verify that we see correct visited count
  // in Rewards panel
  browser()->tab_strip_model()->ActivateTabAt(1);
  rewards_browsertest_util::WaitForElementToContain(
      context_helper_->OpenRewardsPopup().get(),
      "[data-test-id=publishers-count]",
      "This month, you've visited 1 creator supported by Brave Rewards");
}

// TODO(https://github.com/brave/brave-browser/issues/29480): Test flaky on
// master for the mac build.
#if BUILDFLAG(IS_MAC)
#define MAYBE_AutoContributionUnconnectedJapan \
  DISABLED_AutoContributionUnconnectedJapan
#else
#define MAYBE_AutoContributionUnconnectedJapan AutoContributionUnconnectedJapan
#endif  // BUILDFLAG(IS_MAC)
IN_PROC_BROWSER_TEST_F(RewardsContributionBrowserTest,
                       MAYBE_AutoContributionUnconnectedJapan) {
  // Set kEnabled to false before calling CreateRewardsWallet to ensure that
  // prefs are configured to reflect an unconnected user
  auto* pref_service = browser()->profile()->GetPrefs();
  pref_service->SetBoolean(brave_rewards::prefs::kEnabled, false);
  rewards_browsertest_util::CreateRewardsWallet(rewards_service_, "JP");

  // Ensure that auto-contribution is disabled
  ASSERT_FALSE(
      pref_service->GetBoolean(brave_rewards::prefs::kAutoContributeEnabled));

  // Visit publisher (this opens a new tab at index 1)
  rewards_browsertest_util::NavigateToPublisherPage(
      browser(), https_server_.get(), "duckduckgo.com");

  // The minimum publisher duration when testing is 1 second (and the
  // granularity is seconds), so wait for just over 2 seconds to elapse
  base::PlatformThread::Sleep(base::Milliseconds(2100));

  // Switch to original tab to trigger saving publisher activity
  browser()->tab_strip_model()->ActivateTabAt(0);

  rewards_service_->StartContributionsForTesting();

  // Switch back to publisher tab and verify that we see correct visited count
  // in Rewards panel
  browser()->tab_strip_model()->ActivateTabAt(1);
  rewards_browsertest_util::WaitForElementToContain(
      context_helper_->OpenRewardsPopup().get(),
      "[data-test-id=publishers-count]",
      "This month, you've visited 1 creator supported by Brave Rewards");
}

IN_PROC_BROWSER_TEST_F(RewardsContributionBrowserTest,
                       AutoContributionMultiplePublishers) {
  rewards_browsertest_util::CreateRewardsWallet(rewards_service_);
  rewards_service_->SetAutoContributeEnabled(true);
  context_helper_->LoadRewardsPage();
  contribution_->AddBalance(promotion_->ClaimPromotionViaCode());

  context_helper_->VisitPublisher(
      rewards_browsertest_util::GetUrl(https_server_.get(), "duckduckgo.com"),
      true);
  context_helper_->VisitPublisher(
      rewards_browsertest_util::GetUrl(
          https_server_.get(),
          "laurenwags.github.io"),
      true);
  context_helper_->VisitPublisher(
      rewards_browsertest_util::GetUrl(https_server_.get(), "site1.com"),
      true);
  context_helper_->VisitPublisher(
      rewards_browsertest_util::GetUrl(https_server_.get(), "site2.com"),
      true);
  context_helper_->VisitPublisher(
      rewards_browsertest_util::GetUrl(https_server_.get(), "site3.com"),
      true);
  context_helper_->VisitPublisher(
      rewards_browsertest_util::GetUrl(https_server_.get(), "3zsistemi.si"),
      true);

  rewards_service_->StartContributionsForTesting();

  contribution_->WaitForACReconcileCompleted();
  ASSERT_EQ(contribution_->GetACStatus(), ledger::mojom::Result::LEDGER_OK);

  contribution_->IsBalanceCorrect();

  rewards_browsertest_util::WaitForElementToContain(
      contents(), "[data-test-id=rewards-summary-ac]", "-20.00 BAT");

  context_helper_->LoadURL(rewards_browsertest_util::GetRewardsInternalsUrl());

  rewards_browsertest_util::WaitForElementThenClick(
      contents(),
      "#internals-tabs > div > div:nth-of-type(4)");

  for (int i = 1; i <= 6; i++) {
    const std::string query = base::StringPrintf(
        "[data-test-id='publisher-wrapper'] > div:nth-of-type(%d) "
        "[data-test-id='contributed-amount']",
        i);
    LOG(ERROR) << query;
    EXPECT_NE(
      rewards_browsertest_util::WaitForElementThenGetContent(contents(), query),
      "0 BAT");
  }
}

IN_PROC_BROWSER_TEST_F(RewardsContributionBrowserTest,
                       AutoContributionMultiplePublishersUphold) {
  rewards_browsertest_util::CreateRewardsWallet(rewards_service_);
  rewards_service_->SetAutoContributeEnabled(true);
  context_helper_->LoadRewardsPage();
  contribution_->SetUpUpholdWallet(rewards_service_, 50.0);

  std::vector<ledger::mojom::SKUOrderItemPtr> items;
  auto item = ledger::mojom::SKUOrderItem::New();
  item->order_item_id = "ed193339-e58c-483c-8d61-7decd3c24827";
  item->order_id = "a38b211b-bf78-42c8-9479-b11e92e3a76c";
  item->quantity = 80;
  item->price = 0.25;
  item->description = "description";
  item->type = ledger::mojom::SKUOrderItemType::SINGLE_USE;
  items.push_back(std::move(item));

  auto order = ledger::mojom::SKUOrder::New();
  order->order_id = "a38b211b-bf78-42c8-9479-b11e92e3a76c";
  order->total_amount = 20;
  order->merchant_id = "";
  order->location = "brave.com";
  order->items = std::move(items);
  response_->SetSKUOrder(std::move(order));

  context_helper_->VisitPublisher(
      rewards_browsertest_util::GetUrl(https_server_.get(), "duckduckgo.com"),
      true);
  context_helper_->VisitPublisher(
      rewards_browsertest_util::GetUrl(
          https_server_.get(),
          "laurenwags.github.io"),
      true);

  rewards_service_->StartContributionsForTesting();

  contribution_->WaitForACReconcileCompleted();
  ASSERT_EQ(contribution_->GetACStatus(), ledger::mojom::Result::LEDGER_OK);

  contribution_->IsBalanceCorrect();

  rewards_browsertest_util::WaitForElementToContain(
      contents(), "[data-test-id=rewards-summary-ac]", "-20.00 BAT");
}

IN_PROC_BROWSER_TEST_F(RewardsContributionBrowserTest,
                       AutoContributeWhenACOff) {
  rewards_browsertest_util::CreateRewardsWallet(rewards_service_);
  rewards_service_->SetAutoContributeEnabled(true);
  context_helper_->LoadRewardsPage();
  contribution_->AddBalance(promotion_->ClaimPromotionViaCode());

  context_helper_->VisitPublisher(
      rewards_browsertest_util::GetUrl(https_server_.get(), "duckduckgo.com"),
      true);

  rewards_browsertest_util::WaitForElementThenClick(
      contents(),
      "[data-test-id=auto-contribute-panel] "
      "[data-test-id=setting-enabled-toggle] button");

  rewards_service_->StartContributionsForTesting();
}

IN_PROC_BROWSER_TEST_F(RewardsContributionBrowserTest, TipVerifiedPublisher) {
  rewards_browsertest_util::CreateRewardsWallet(rewards_service_);
  context_helper_->LoadRewardsPage();
  contribution_->AddBalance(promotion_->ClaimPromotionViaCode());

  contribution_->TipPublisher(
      rewards_browsertest_util::GetUrl(https_server_.get(), "duckduckgo.com"),
      false, 1);
}

IN_PROC_BROWSER_TEST_F(RewardsContributionBrowserTest,
                       TipVerifiedPublisherWithCustomAmount) {
  rewards_browsertest_util::CreateRewardsWallet(rewards_service_);
  context_helper_->LoadRewardsPage();
  contribution_->AddBalance(promotion_->ClaimPromotionViaCode());

  contribution_->TipPublisher(
      rewards_browsertest_util::GetUrl(https_server_.get(), "duckduckgo.com"),
      false, 1, 0, 1.25);
}

IN_PROC_BROWSER_TEST_F(RewardsContributionBrowserTest, TipUnverifiedPublisher) {
  rewards_browsertest_util::CreateRewardsWallet(rewards_service_);
  context_helper_->LoadRewardsPage();
  contribution_->AddBalance(promotion_->ClaimPromotionViaCode());

  contribution_->TipPublisher(
      rewards_browsertest_util::GetUrl(https_server_.get(), "brave.com"),
      false);
}

IN_PROC_BROWSER_TEST_F(RewardsContributionBrowserTest,
                       RecurringTipForVerifiedPublisher) {
  rewards_browsertest_util::CreateRewardsWallet(rewards_service_);
  context_helper_->LoadRewardsPage();
  contribution_->AddBalance(promotion_->ClaimPromotionViaCode());

  contribution_->TipPublisher(
      rewards_browsertest_util::GetUrl(https_server_.get(), "duckduckgo.com"),
      true, 1);
}

IN_PROC_BROWSER_TEST_F(RewardsContributionBrowserTest, TipWithVerifiedWallet) {
  rewards_browsertest_util::CreateRewardsWallet(rewards_service_);
  contribution_->SetUpUpholdWallet(rewards_service_, 50.0);

  const double amount = 5.0;
  contribution_->TipViaCode("duckduckgo.com", amount,
                            ledger::mojom::PublisherStatus::UPHOLD_VERIFIED);
  contribution_->VerifyTip(amount, false, true);
}

// TODO(https://github.com/brave/brave-browser/issues/12555): This test is known
// to fail intermittently. The likely cause is that after waiting for tips to
// reconcile, one or both of the generated fees may have already been removed
// from the ExternalWallet data.
IN_PROC_BROWSER_TEST_F(
    RewardsContributionBrowserTest,
    DISABLED_MultipleTipsProduceMultipleFeesWithVerifiedWallet) {
  rewards_browsertest_util::CreateRewardsWallet(rewards_service_);
  contribution_->SetUpUpholdWallet(rewards_service_, 50.0);

  double total_amount = 0.0;
  const double amount = 5.0;
  const double fee_percentage = 0.05;
  const double tip_fee = amount * fee_percentage;
  contribution_->TipViaCode("duckduckgo.com", amount,
                            ledger::mojom::PublisherStatus::UPHOLD_VERIFIED);
  total_amount += amount;

  contribution_->TipViaCode("laurenwags.github.io", amount,
                            ledger::mojom::PublisherStatus::UPHOLD_VERIFIED);
  total_amount += amount;

  base::RunLoop run_loop_first;
  rewards_service_->GetExternalWallet(base::BindLambdaForTesting(
      [&](brave_rewards::GetExternalWalletResult result) {
        const auto wallet = std::move(result).value_or(nullptr);
        ASSERT_TRUE(wallet);
        ASSERT_EQ(wallet->fees.size(), 2UL);
        for (auto const& value : wallet->fees) {
          ASSERT_EQ(value.second, tip_fee);
        }
        run_loop_first.Quit();
      }));
  run_loop_first.Run();
  contribution_->VerifyTip(total_amount, false, true);
}

// Ensure that we can make a one-time tip of a non-integral amount.
IN_PROC_BROWSER_TEST_F(RewardsContributionBrowserTest, TipNonIntegralAmount) {
  rewards_browsertest_util::CreateRewardsWallet(rewards_service_);
  context_helper_->LoadRewardsPage();
  contribution_->AddBalance(promotion_->ClaimPromotionViaCode());

  rewards_service_->SendContribution("duckduckgo.com", 2.5, false,
                                     base::DoNothing());
  contribution_->WaitForTipReconcileCompleted();
  ASSERT_EQ(contribution_->GetTipStatus(), ledger::mojom::Result::LEDGER_OK);
  ASSERT_EQ(contribution_->GetReconcileTipTotal(), 2.5);
}

// Ensure that we can make a recurring tip of a non-integral amount.
IN_PROC_BROWSER_TEST_F(RewardsContributionBrowserTest,
                       RecurringTipNonIntegralAmount) {
  rewards_browsertest_util::CreateRewardsWallet(rewards_service_);
  rewards_service_->SetAutoContributeEnabled(true);
  context_helper_->LoadRewardsPage();
  contribution_->AddBalance(promotion_->ClaimPromotionViaCode());

  const bool verified = true;
  context_helper_->VisitPublisher(
      rewards_browsertest_util::GetUrl(https_server_.get(), "duckduckgo.com"),
      verified);

  rewards_service_->SendContribution("duckduckgo.com", 2.5, true,
                                     base::DoNothing());
  rewards_service_->StartContributionsForTesting();
  contribution_->WaitForTipReconcileCompleted();
  ASSERT_EQ(contribution_->GetTipStatus(), ledger::mojom::Result::LEDGER_OK);

  ASSERT_EQ(contribution_->GetReconcileTipTotal(), 2.5);
}

IN_PROC_BROWSER_TEST_F(RewardsContributionBrowserTest,
                       RecurringAndPartialAutoContribution) {
  rewards_browsertest_util::CreateRewardsWallet(rewards_service_);
  rewards_service_->SetAutoContributeEnabled(true);
  context_helper_->LoadRewardsPage();
  contribution_->AddBalance(promotion_->ClaimPromotionViaCode());

  // Visit verified publisher
  const bool verified = true;
  context_helper_->VisitPublisher(
      rewards_browsertest_util::GetUrl(https_server_.get(), "duckduckgo.com"),
      true);

  // Set monthly recurring
  contribution_->TipViaCode("duckduckgo.com", 25.0,
                            ledger::mojom::PublisherStatus::UPHOLD_VERIFIED,
                            true);

  context_helper_->VisitPublisher(
      rewards_browsertest_util::GetUrl(https_server_.get(), "brave.com"),
      !verified);

  // Trigger contribution process
  rewards_service_->StartContributionsForTesting();

  // Wait for reconciliation to complete
  contribution_->WaitForTipReconcileCompleted();
  ASSERT_EQ(contribution_->GetTipStatus(), ledger::mojom::Result::LEDGER_OK);

  // Wait for reconciliation to complete successfully
  contribution_->WaitForACReconcileCompleted();
  ASSERT_EQ(contribution_->GetACStatus(), ledger::mojom::Result::LEDGER_OK);

  // Make sure that balance is updated correctly
  contribution_->IsBalanceCorrect();

  // Check that summary table shows the appropriate contribution
  rewards_browsertest_util::WaitForElementToContain(
      contents(), "[data-test-id=rewards-summary-ac]", "-5.00 BAT");
}

IN_PROC_BROWSER_TEST_F(RewardsContributionBrowserTest,
                       MultipleRecurringOverBudgetAndPartialAutoContribution) {
  rewards_browsertest_util::CreateRewardsWallet(rewards_service_);
  rewards_service_->SetAutoContributeEnabled(true);
  context_helper_->LoadRewardsPage();
  contribution_->AddBalance(promotion_->ClaimPromotionViaCode());

  contribution_->TipViaCode("duckduckgo.com", 3.0,
                            ledger::mojom::PublisherStatus::UPHOLD_VERIFIED,
                            true);

  contribution_->TipViaCode(
      "site1.com", 5.0, ledger::mojom::PublisherStatus::UPHOLD_VERIFIED, true);

  contribution_->TipViaCode(
      "site2.com", 5.0, ledger::mojom::PublisherStatus::UPHOLD_VERIFIED, true);

  contribution_->TipViaCode(
      "site3.com", 5.0, ledger::mojom::PublisherStatus::UPHOLD_VERIFIED, true);

  const bool verified = true;
  context_helper_->VisitPublisher(
      rewards_browsertest_util::GetUrl(https_server_.get(), "duckduckgo.com"),
      verified);

  // Trigger contribution process
  rewards_service_->StartContributionsForTesting();

  // Wait for reconciliation to complete
  contribution_->WaitForMultipleTipReconcileCompleted(3);
  ASSERT_EQ(contribution_->GetTipStatus(), ledger::mojom::Result::LEDGER_OK);

  // Wait for reconciliation to complete successfully
  contribution_->WaitForACReconcileCompleted();
  ASSERT_EQ(contribution_->GetACStatus(), ledger::mojom::Result::LEDGER_OK);

  // Make sure that balance is updated correctly
  contribution_->IsBalanceCorrect();

  // Check that summary table shows the appropriate contribution
  rewards_browsertest_util::WaitForElementToContain(
      contents(), "[data-test-id=rewards-summary-ac]", "-4.00 BAT");
}

IN_PROC_BROWSER_TEST_F(RewardsContributionBrowserTest,
                       DISABLED_SplitProcessorAutoContribution) {
  rewards_browsertest_util::CreateRewardsWallet(rewards_service_);
  rewards_service_->SetAutoContributeEnabled(true);
  context_helper_->LoadRewardsPage();
  contribution_->SetUpUpholdWallet(rewards_service_, 50.0);
  contribution_->AddBalance(promotion_->ClaimPromotionViaCode());

  context_helper_->VisitPublisher(
      rewards_browsertest_util::GetUrl(https_server_.get(), "3zsistemi.si"),
      true);

  // 30 form unblinded and 20 from uphold
  rewards_service_->SetAutoContributionAmount(50.0);

  std::vector<ledger::mojom::SKUOrderItemPtr> items;
  auto item = ledger::mojom::SKUOrderItem::New();
  item->order_item_id = "ed193339-e58c-483c-8d61-7decd3c24827";
  item->order_id = "a38b211b-bf78-42c8-9479-b11e92e3a76c";
  item->quantity = 80;
  item->price = 0.25;
  item->description = "description";
  item->type = ledger::mojom::SKUOrderItemType::SINGLE_USE;
  items.push_back(std::move(item));

  auto order = ledger::mojom::SKUOrder::New();
  order->order_id = "a38b211b-bf78-42c8-9479-b11e92e3a76c";
  order->total_amount = 20;
  order->merchant_id = "";
  order->location = "brave.com";
  order->items = std::move(items);
  response_->SetSKUOrder(std::move(order));

  // Trigger contribution process
  rewards_service_->StartContributionsForTesting();

  // Wait for reconciliation to complete successfully
  contribution_->WaitForMultipleACReconcileCompleted(2);
  auto statuses = contribution_->GetMultipleACStatus();
  ASSERT_EQ(statuses[0], ledger::mojom::Result::LEDGER_OK);
  ASSERT_EQ(statuses[1], ledger::mojom::Result::LEDGER_OK);

  // Wait for UI to update with contribution
  rewards_browsertest_util::WaitForElementToContain(
      contents(), "[data-test-id=rewards-summary-ac]", "-50.00 BAT");

  rewards_browsertest_util::WaitForElementThenClick(
      contents(), "[data-test-id=view-statement-button]");

  rewards_browsertest_util::WaitForElementToAppear(
      contents(),
      "#transactionTable");

  rewards_browsertest_util::WaitForElementToContain(
      contents(),
      "#transactionTable",
      "-30.000BAT");

  rewards_browsertest_util::WaitForElementToContain(
      contents(),
      "#transactionTable",
      "-20.000BAT");
}

IN_PROC_BROWSER_TEST_F(RewardsContributionBrowserTest,
                       CheckIfReconcileWasReset) {
  rewards_browsertest_util::CreateRewardsWallet(rewards_service_);
  rewards_service_->SetAutoContributeEnabled(true);
  context_helper_->LoadRewardsPage();
  uint64_t current_stamp = 0;

  base::RunLoop run_loop_first;
  rewards_service_->GetReconcileStamp(
      base::BindLambdaForTesting([&](uint64_t stamp) {
        current_stamp = stamp;
        run_loop_first.Quit();
      }));
  run_loop_first.Run();
  contribution_->AddBalance(promotion_->ClaimPromotionViaCode());

  context_helper_->VisitPublisher(
      rewards_browsertest_util::GetUrl(https_server_.get(), "duckduckgo.com"),
      true);

  contribution_->TipPublisher(
      rewards_browsertest_util::GetUrl(https_server_.get(), "duckduckgo.com"),
      true, 1);

  base::RunLoop run_loop_second;
  rewards_service_->GetReconcileStamp(
      base::BindLambdaForTesting([&](uint64_t stamp) {
        ASSERT_NE(current_stamp, stamp);
        run_loop_second.Quit();
      }));
  run_loop_second.Run();
}

IN_PROC_BROWSER_TEST_F(RewardsContributionBrowserTest,
                       CheckIfReconcileWasResetACOff) {
  rewards_browsertest_util::CreateRewardsWallet(rewards_service_);
  context_helper_->LoadRewardsPage();
  uint64_t current_stamp = 0;

  base::RunLoop run_loop_first;
  rewards_service_->GetReconcileStamp(
      base::BindLambdaForTesting([&](uint64_t stamp) {
        current_stamp = stamp;
        run_loop_first.Quit();
      }));
  run_loop_first.Run();
  contribution_->AddBalance(promotion_->ClaimPromotionViaCode());
  contribution_->TipPublisher(
      rewards_browsertest_util::GetUrl(https_server_.get(), "duckduckgo.com"),
      true, 1);

  base::RunLoop run_loop_second;
  rewards_service_->GetReconcileStamp(
      base::BindLambdaForTesting([&](uint64_t stamp) {
        ASSERT_NE(current_stamp, stamp);
        run_loop_second.Quit();
      }));
  run_loop_second.Run();
}

IN_PROC_BROWSER_TEST_F(RewardsContributionBrowserTest, PanelMonthlyTipAmount) {
  rewards_browsertest_util::CreateRewardsWallet(rewards_service_);
  context_helper_->LoadRewardsPage();
  contribution_->AddBalance(promotion_->ClaimPromotionViaCode());

  rewards_browsertest_util::NavigateToPublisherPage(
      browser(),
      https_server_.get(),
      "3zsistemi.si");

  // Add a recurring tip of 10 BAT.
  contribution_->TipViaCode("3zsistemi.si", 10.0,
                            ledger::mojom::PublisherStatus::UPHOLD_VERIFIED,
                            true);

  // Verify current tip amount displayed on panel
  base::WeakPtr<content::WebContents> popup =
      context_helper_->OpenRewardsPopup();
  const double tip_amount =
      rewards_browsertest_util::GetRewardsPopupMonthlyTipValue(popup.get());
  ASSERT_EQ(tip_amount, 10.0);
}

}  // namespace rewards_browsertest
