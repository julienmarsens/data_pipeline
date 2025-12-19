#include <Rcpp.h>
#include <cmath>
using namespace Rcpp;

// [[Rcpp::export]]
List nc2lInventoryControl(
    NumericVector aBidA, NumericVector anAskA,
    NumericVector aBidB, NumericVector anAskB,
    NumericVector aSellLevelA, NumericVector aBuyLevelA,
    NumericVector aSellLevelB, NumericVector aBuyLevelB,
    int nc2l, double aBaseOrderSizeA, double aBaseOrderSizeB,
    double growth_k = 1, double max_mult = 5.0,
    double ref_disp = NA_REAL    // optional normalization reference
) {
  int n = aSellLevelA.size();
  const double PREC = 1e-5;

  double maxInvA = nc2l * aBaseOrderSizeA;
  double maxInvB = nc2l * aBaseOrderSizeB;

  NumericVector orderA(n), orderB(n);
  NumericVector posA(n), posB(n);

  // --- compute baseline dispersion (for normalization)
  NumericVector dispersion(n);
  for (int i = 0; i < n; ++i)
    dispersion[i] = std::fabs(aBidA[i] - aBidB[i]);

  double meanDisp = 0.0;
  for (int i = 0; i < n; ++i) meanDisp += dispersion[i];
  meanDisp /= n;

  if (R_IsNA(ref_disp)) ref_disp = meanDisp;
  if (ref_disp < 1e-12) ref_disp = 1e-12;

  // --- helper: size scaling by dispersion
  auto disp_adj_size = [&](double baseSize, double disp) {
    double norm = disp / ref_disp;
    // limit the factor smoothly between 1 and max_mult
    double factor = 1.0 + growth_k * std::tanh(norm - 1.0);
    if (factor < 0.2) factor = 0.2;
    if (factor > max_mult) factor = max_mult;
    return baseSize * factor;
  };

  // --- main loop ---
  for (int i = 0; i < n; ++i) {
    if (i == 0) {
      orderA[i] = orderB[i] = posA[i] = posB[i] = 0.0;
      continue;
    }

    // carry inventory forward
    posA[i] = posA[i - 1];
    posB[i] = posB[i - 1];

    double disp = dispersion[i];

    // ==== (1) Upward cross on A (SELL A / BUY B) ====
    if ((aSellLevelA[i - 1] - aBidA[i]) <= PREC) {
      double sizeA = disp_adj_size(aBaseOrderSizeA, disp);
      double sizeB = disp_adj_size(aBaseOrderSizeB, disp);

      if (maxInvA >= fabs(posA[i - 1] - sizeA) &&
          maxInvB >= fabs(posB[i - 1] + sizeB)) {
        orderA[i] = -sizeA;
        orderB[i] =  sizeB;
        posA[i] -= sizeA;
        posB[i] += sizeB;
      }
    }

    // ==== (2) Downward cross on A (BUY A / SELL B) ====
    if ((aBuyLevelA[i - 1] - anAskA[i]) >= -PREC) {
      double sizeA = disp_adj_size(aBaseOrderSizeA, disp);
      double sizeB = disp_adj_size(aBaseOrderSizeB, disp);

      if (maxInvA >= fabs(posA[i - 1] + sizeA) &&
          maxInvB >= fabs(posB[i - 1] - sizeB)) {
        orderA[i] =  sizeA;
        orderB[i] = -sizeB;
        posA[i] += sizeA;
        posB[i] -= sizeB;
      }
    }

    // ==== (3) Upward cross on B (SELL B / BUY A) ====
    if ((aSellLevelB[i - 1] - aBidB[i]) <= PREC) {
      double sizeA = disp_adj_size(aBaseOrderSizeA, disp);
      double sizeB = disp_adj_size(aBaseOrderSizeB, disp);

      if (maxInvA >= fabs(posA[i - 1] + sizeA) &&
          maxInvB >= fabs(posB[i - 1] - sizeB)) {
        orderA[i] =  sizeA;
        orderB[i] = -sizeB;
        posA[i] += sizeA;
        posB[i] -= sizeB;
      }
    }

    // ==== (4) Downward cross on B (BUY B / SELL A) ====
    if ((aBuyLevelB[i - 1] - anAskB[i]) >= -PREC) {
      double sizeA = disp_adj_size(aBaseOrderSizeA, disp);
      double sizeB = disp_adj_size(aBaseOrderSizeB, disp);

      if (maxInvA >= fabs(posA[i - 1] - sizeA) &&
          maxInvB >= fabs(posB[i - 1] + sizeB)) {
        orderA[i] = -sizeA;
        orderB[i] =  sizeB;
        posA[i] -= sizeA;
        posB[i] += sizeB;
      }
    }
  }

  return List::create(
    Named("theOrderVecA") = orderA,
    Named("theOrderVecB") = orderB,
    Named("thePositionVecA") = posA,
    Named("thePositionVecB") = posB
  );
}
