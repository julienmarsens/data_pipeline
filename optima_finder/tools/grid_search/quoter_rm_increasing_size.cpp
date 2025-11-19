#include <Rcpp.h>
#include <cmath>
using namespace Rcpp;

// [[Rcpp::export]]
List nc2lInventoryControl(NumericVector aBidA, NumericVector anAskA,
                          NumericVector aBidB, NumericVector anAskB,
                          NumericVector aSellLevelA, NumericVector aBuyLevelA,
                          NumericVector aSellLevelB, NumericVector aBuyLevelB,
                          int nc2l, double aBaseOrderSizeA, double aBaseOrderSizeB,
                          double growth_k = 0.5, double max_mult = 3.0) {

  int n = aSellLevelA.size();
  float thePrecision_Prices = 1.e-5;

  double maxInventoryA = nc2l * aBaseOrderSizeA;
  double maxInventoryB = nc2l * aBaseOrderSizeB;

  NumericVector theOrderVecA(n);
  NumericVector theOrderVecB(n);
  NumericVector thePositionVecA(n);
  NumericVector thePositionVecB(n);

  // --- helper: stat-arb style size function (amplifies same-side trades)
    auto inv_adj_size = [&](double baseSize, double inv, int sideSign) {
      double mag = std::fabs(inv / baseSize);
      double factor;

      if (sideSign * inv > 0) {
        // Same-side trade: pyramiding toward convergence
        factor = 1.0 + growth_k * mag;
      } else {
        // Opposite-side trade: aggressive unwinding
        factor = 1.0 + growth_k * mag * 0.8;  // 0.8 balances aggressiveness
      }

      if (factor > max_mult) factor = max_mult;
      return baseSize * factor;
    };

//  auto inv_adj_size = [&](double baseSize, double inv, int sideSign) {
//  double mag = std::tanh(growth_k * std::fabs(inv / baseSize));
//  double factor = 1.0 + (max_mult - 1.0) * mag;
//  return baseSize * factor;
//};

  for (int i = 0; i < n; ++i) {
    if (i == 0) {
      theOrderVecA[i] = 0.0;
      theOrderVecB[i] = 0.0;
      thePositionVecA[i] = 0.0;
      thePositionVecB[i] = 0.0;
      continue;
    }

    // carry forward inventory
    thePositionVecA[i] = thePositionVecA[i - 1];
    thePositionVecB[i] = thePositionVecB[i - 1];

    double posA = thePositionVecA[i - 1];
    double posB = thePositionVecB[i - 1];

    // ==== (1) Upward cross on A (SELL A / BUY B) ====
    if ((aSellLevelA[i - 1] - aBidA[i]) <= thePrecision_Prices) {
      double sizeA = inv_adj_size(aBaseOrderSizeA, posA, -1);
      double sizeB = inv_adj_size(aBaseOrderSizeB, posB, +1);

      if (maxInventoryA >= fabs(posA - sizeA) &&
          maxInventoryB >= fabs(posB + sizeB)) {
        theOrderVecA[i] = -sizeA;
        theOrderVecB[i] =  sizeB;
        thePositionVecA[i] = posA - sizeA;
        thePositionVecB[i] = posB + sizeB;
      }
    }

    // ==== (2) Downward cross on A (BUY A / SELL B) ====
    if ((aBuyLevelA[i - 1] - anAskA[i]) >= -thePrecision_Prices) {
      double sizeA = inv_adj_size(aBaseOrderSizeA, posA, +1);
      double sizeB = inv_adj_size(aBaseOrderSizeB, posB, -1);

      if (maxInventoryA >= fabs(posA + sizeA) &&
          maxInventoryB >= fabs(posB - sizeB)) {
        theOrderVecA[i] =  sizeA;
        theOrderVecB[i] = -sizeB;
        thePositionVecA[i] = posA + sizeA;
        thePositionVecB[i] = posB - sizeB;
      }
    }

    // ==== (3) Cross on B (SELL B / BUY A) ====
    if ((aSellLevelB[i - 1] - aBidB[i]) <= thePrecision_Prices) {
      double sizeA = inv_adj_size(aBaseOrderSizeA, posA, +1);
      double sizeB = inv_adj_size(aBaseOrderSizeB, posB, -1);

      if (maxInventoryA >= fabs(posA + sizeA) &&
          maxInventoryB >= fabs(posB - sizeB)) {
        theOrderVecA[i] =  sizeA;
        theOrderVecB[i] = -sizeB;
        thePositionVecA[i] = posA + sizeA;
        thePositionVecB[i] = posB - sizeB;
      }
    }

    // ==== (4) Cross on B (BUY B / SELL A) ====
    if ((aBuyLevelB[i - 1] - anAskB[i]) >= -thePrecision_Prices) {
      double sizeA = inv_adj_size(aBaseOrderSizeA, posA, -1);
      double sizeB = inv_adj_size(aBaseOrderSizeB, posB, +1);

      if (maxInventoryA >= fabs(posA - sizeA) &&
          maxInventoryB >= fabs(posB + sizeB)) {
        theOrderVecA[i] = -sizeA;
        theOrderVecB[i] =  sizeB;
        thePositionVecA[i] = posA - sizeA;
        thePositionVecB[i] = posB + sizeB;
      }
    }
  }

  return List::create(
    Named("theOrderVecA") = theOrderVecA,
    Named("theOrderVecB") = theOrderVecB,
    Named("thePositionVecA") = thePositionVecA,
    Named("thePositionVecB") = thePositionVecB
  );
}
