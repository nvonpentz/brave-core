/* Copyright (c) 2020 The Brave Authors. All rights reserved.
 * This Source Code Form is subject to the terms of the Mozilla Public
 * License, v. 2.0. If a copy of the MPL was not distributed with this file,
 * You can obtain one at https://mozilla.org/MPL/2.0/. */

#include "brave/components/brave_ads/core/internal/common/numbers/number_util.h"

#include <cmath>

namespace brave_ads {

namespace {

constexpr double kEpsilon = .00001;
constexpr int kDecimalPlaces = 4;

double RoundDoubleNPlaces(const double value, const int n_places) {
  const double pow_10 = pow(10.0F, static_cast<double>(n_places));
  return round(value * pow_10) / pow_10;
}

}  // namespace

bool DoubleEquals(const double lhs, const double rhs) {
  // Choice of epsilon for double comparison allows for proper comparison, we
  // want it to be relatively high to avoid false negative comparison results
  return std::abs(lhs - rhs) <= kEpsilon;
}

bool DoubleIsGreaterEqual(const double lhs, const double rhs) {
  return RoundDoubleNPlaces(lhs, kDecimalPlaces) >=
         RoundDoubleNPlaces(rhs, kDecimalPlaces);
}

bool DoubleIsGreater(const double lhs, const double rhs) {
  return RoundDoubleNPlaces(lhs, kDecimalPlaces) >
         RoundDoubleNPlaces(rhs, kDecimalPlaces);
}

bool DoubleIsLessEqual(const double lhs, const double rhs) {
  return RoundDoubleNPlaces(lhs, kDecimalPlaces) <=
         RoundDoubleNPlaces(rhs, kDecimalPlaces);
}

bool DoubleIsLess(const double lhs, const double rhs) {
  return RoundDoubleNPlaces(lhs, kDecimalPlaces) <
         RoundDoubleNPlaces(rhs, kDecimalPlaces);
}

}  // namespace brave_ads
