#include <Rcpp.h>
#include <cmath>
#include <vector>
using namespace Rcpp;

// [[Rcpp::export]]
List computeDailyHedgedPnL(
    NumericVector bidA, NumericVector askA,
    NumericVector bidB, NumericVector askB,
    NumericVector aSellLevelA, NumericVector aBuyLevelA,
    NumericVector aSellLevelB, NumericVector aBuyLevelB,
    NumericVector orderVecA, NumericVector orderVecB,
    double minOrderSizeA, double minOrderSizeB,
    double tFeesMakerA, double tFeesMakerB,
    double tFeesTakerA, double tFeesTakerB,
    IntegerVector idxEod,
    bool isDailyHedged) {

  int n = bidA.size();
  double precision = 1.e-5;

  // Step 1: Order rounding (replicates R: sign * floor(abs/min) * min)
  NumericVector cleanOrderA(n, 0.0);
  NumericVector cleanOrderB(n, 0.0);
  for (int i = 0; i < n; i++) {
    double oA = orderVecA[i];
    double oB = orderVecB[i];
    if (oA != 0.0) {
      double signA = (oA > 0) ? 1.0 : -1.0;
      cleanOrderA[i] = signA * std::floor(std::fabs(oA) / minOrderSizeA) * minOrderSizeA;
    }
    if (oB != 0.0) {
      double signB = (oB > 0) ? 1.0 : -1.0;
      cleanOrderB[i] = signB * std::floor(std::fabs(oB) / minOrderSizeB) * minOrderSizeB;
    }
  }

  // Crossing index: true if any order on this tick
  std::vector<bool> idxXing(n, false);
  for (int i = 0; i < n; i++) {
    idxXing[i] = (cleanOrderA[i] != 0.0 || cleanOrderB[i] != 0.0);
  }

  // Step 2: Trade detection (4 crossing conditions, lagged by 1)
  // idx.1: aSellLevelA[i-1] - bidA[i] <= precision (upward cross on A)
  // idx.2: aBuyLevelA[i-1] - askA[i] >= -precision (downward cross on A)
  // idx.3: aSellLevelB[i-1] - bidB[i] <= precision (upward cross on B)
  // idx.4: aBuyLevelB[i-1] - askB[i] >= -precision (downward cross on B)
  std::vector<int> crossType(n, 0); // 0=none, 1=idx1, 2=idx2, 3=idx3, 4=idx4

  // Trade price matrix: [priceA_maker, priceA_fill, priceB_fill, priceB_fill2]
  NumericVector tradePriceA(n, 0.0); // col 2: fill price for A
  NumericVector tradePriceA2(n, 0.0); // col 3: mid/mark price for A
  NumericVector tradePriceB(n, 0.0); // col 4: fill price for B
  NumericVector tradePriceB2(n, 0.0); // col 5: mid/mark price for B

  for (int i = 1; i < n; i++) {
    bool idx1 = (aSellLevelA[i-1] - bidA[i]) <= precision;
    bool idx2 = (aBuyLevelA[i-1] - askA[i]) >= -precision;
    bool idx3 = (aSellLevelB[i-1] - bidB[i]) <= precision;
    bool idx4 = (aBuyLevelB[i-1] - askB[i]) >= -precision;

    // Match R behavior: later assignments overwrite earlier ones
    if (idx1) {
      tradePriceA[i] = aSellLevelA[i-1]; // maker on A
      tradePriceA2[i] = aSellLevelA[i-1];
      tradePriceB[i] = askB[i]; // taker on B
      tradePriceB2[i] = askB[i];
      crossType[i] = 1;
    }
    if (idx2) {
      tradePriceA[i] = aBuyLevelA[i-1]; // maker on A
      tradePriceA2[i] = aBuyLevelA[i-1];
      tradePriceB[i] = bidB[i]; // taker on B
      tradePriceB2[i] = bidB[i];
      crossType[i] = 2;
    }
    if (idx3) {
      tradePriceA[i] = askA[i]; // taker on A
      tradePriceA2[i] = askA[i];
      tradePriceB[i] = aSellLevelB[i-1]; // maker on B
      tradePriceB2[i] = aSellLevelB[i-1];
      crossType[i] = 3;
    }
    if (idx4) {
      tradePriceA[i] = bidA[i]; // taker on A
      tradePriceA2[i] = bidA[i];
      tradePriceB[i] = aBuyLevelB[i-1]; // maker on B
      tradePriceB2[i] = aBuyLevelB[i-1];
      crossType[i] = 4;
    }
  }

  // Step 3: Transaction fee computation
  // For idx1/idx2: A is maker, B is taker
  // For idx3/idx4: A is taker, B is maker
  NumericVector transFeesA(n, 0.0);
  NumericVector transFeesB(n, 0.0);
  NumericVector orderFeesA(n, 0.0);
  NumericVector orderFeesB(n, 0.0);

  for (int i = 1; i < n; i++) {
    bool idx1 = (aSellLevelA[i-1] - bidA[i]) <= precision;
    bool idx2 = (aBuyLevelA[i-1] - askA[i]) >= -precision;
    bool idx3 = (aSellLevelB[i-1] - bidB[i]) <= precision;
    bool idx4 = (aBuyLevelB[i-1] - askB[i]) >= -precision;

    // Order fees: R uses offset indexing (which(idx.1) maps to i-1 in 0-based)
    // In R: order.fees[which(idx.1),1] <- clean.order.rounded[which(idx.1)+1,1]
    // In R: order.fees[which(idx.1)+1,2] <- clean.order.rounded[which(idx.1)+1,2]
    if (idx1) {
      orderFeesA[i-1] = cleanOrderA[i];
      orderFeesB[i] = cleanOrderB[i];
      transFeesA[i] = std::fabs(cleanOrderA[i]) * tFeesMakerA; // A is maker
      transFeesB[i] = std::fabs(cleanOrderB[i]) * tFeesTakerB; // B is taker
    }
    if (idx2) {
      orderFeesA[i-1] = cleanOrderA[i];
      orderFeesB[i] = cleanOrderB[i];
      transFeesA[i] = std::fabs(cleanOrderA[i]) * tFeesMakerA;
      transFeesB[i] = std::fabs(cleanOrderB[i]) * tFeesTakerB;
    }
    if (idx3) {
      orderFeesA[i] = cleanOrderA[i];
      orderFeesB[i-1] = cleanOrderB[i];
      transFeesA[i] = std::fabs(cleanOrderA[i]) * tFeesTakerA; // A is taker
      transFeesB[i] = std::fabs(cleanOrderB[i]) * tFeesMakerB; // B is maker
    }
    if (idx4) {
      orderFeesA[i] = cleanOrderA[i];
      orderFeesB[i-1] = cleanOrderB[i];
      transFeesA[i] = std::fabs(cleanOrderA[i]) * tFeesTakerA;
      transFeesB[i] = std::fabs(cleanOrderB[i]) * tFeesMakerB;
    }
  }

  if (!isDailyHedged) {
    // Version 2.0: non-daily-hedged PnL (simple cumulative)
    // Count crossings
    std::vector<int> xingIndices;
    for (int i = 0; i < n; i++) {
      if (idxXing[i]) xingIndices.push_back(i);
    }
    int nc = xingIndices.size();
    if (nc == 0) {
      return List::create(
        Named("pnl_wo_mh") = NumericVector(0),
        Named("daily_day_index") = IntegerVector(0),
        Named("num_crossings") = 0
      );
    }

    NumericVector pnlCumA(nc), pnlCumB(nc);
    double cumContractA = 0, cumBaseA = 0;
    double cumContractB = 0, cumBaseB = 0;
    for (int j = 0; j < nc; j++) {
      int i = xingIndices[j];
      double tFeesBcA = transFeesA[i] / tradePriceA2[i];
      double tFeesBcB = transFeesB[i] / tradePriceB2[i];
      cumContractA += -cleanOrderA[i];
      cumBaseA += cleanOrderA[i] / tradePriceA[i] - tFeesBcA;
      cumContractB += -cleanOrderB[i];
      cumBaseB += cleanOrderB[i] / tradePriceB[i] - tFeesBcB;
      pnlCumA[j] = cumContractA + cumBaseA * tradePriceA2[i];
      pnlCumB[j] = cumContractB + cumBaseB * tradePriceB2[i];
    }

    NumericVector pnlWoMh(nc);
    for (int j = 0; j < nc; j++) pnlWoMh[j] = pnlCumA[j] + pnlCumB[j];

    return List::create(
      Named("pnl_wo_mh") = pnlWoMh,
      Named("daily_day_index") = IntegerVector(nc, 1),
      Named("num_crossings") = nc
    );
  }

  // Version 3.0: Daily-hedged PnL
  int numDays = idxEod.size();

  // Pre-allocate with upper bound: numDays * 2 (entry+exit) + total crossings
  int totalCrossings = 0;
  for (int i = 0; i < n; i++) {
    if (idxXing[i]) totalCrossings++;
  }
  int maxTrades = numDays * 2 + totalCrossings;

  std::vector<double> tsA_pos(maxTrades), tsA_price(maxTrades);
  std::vector<double> tsB_pos(maxTrades), tsB_price(maxTrades);
  std::vector<double> feesA(maxTrades, 0.0), feesB(maxTrades, 0.0);
  std::vector<int> dayIndex(maxTrades);
  int tradeIdx = 0;

  double prevQuotePosA = 0.0;
  double prevQuotePosB = 0.0;
  double liquidationMidA = 0.0;
  double liquidationMidB = 0.0;
  bool firstDay = true;

  for (int day = 0; day < numDays; day++) {
    int dayStart = (day == 0) ? 0 : idxEod[day - 1];
    int dayEnd = idxEod[day] - 1; // Convert from R 1-based to C++ 0-based

    // Entry mid price
    double entryMidA, entryMidB;
    if (firstDay) {
      entryMidA = (bidA[dayStart] + askA[dayStart]) / 2.0;
      entryMidB = (bidB[dayStart] + askB[dayStart]) / 2.0;
      firstDay = false;
    } else {
      entryMidA = liquidationMidA;
      entryMidB = liquidationMidB;
    }

    // Liquidation mid price (last tick of day)
    liquidationMidA = (bidA[dayEnd] + askA[dayEnd]) / 2.0;
    liquidationMidB = (bidB[dayEnd] + askB[dayEnd]) / 2.0;

    // Check if any crossings in this day
    bool hasCrossings = false;
    for (int i = dayStart; i <= dayEnd; i++) {
      if (idxXing[i]) { hasCrossings = true; break; }
    }

    // Entry trade row
    tsA_pos[tradeIdx] = prevQuotePosA;
    tsA_price[tradeIdx] = entryMidA;
    tsB_pos[tradeIdx] = prevQuotePosB;
    tsB_price[tradeIdx] = entryMidB;
    feesA[tradeIdx] = 0.0;
    feesB[tradeIdx] = 0.0;
    dayIndex[tradeIdx] = day + 1; // 1-based for R
    tradeIdx++;

    if (hasCrossings) {
      // Add crossing trades for this day
      for (int i = dayStart; i <= dayEnd; i++) {
        if (idxXing[i]) {
          tsA_pos[tradeIdx] = -cleanOrderA[i];
          tsA_price[tradeIdx] = tradePriceA[i];
          tsB_pos[tradeIdx] = -cleanOrderB[i];
          tsB_price[tradeIdx] = tradePriceB[i];

          // Fee computation (base currency: fee / price)
          double feeA_val = 0.0, feeB_val = 0.0;
          if (tradePriceA2[i] != 0.0)
            feeA_val = transFeesA[i] / tradePriceA2[i];
          if (tradePriceB2[i] != 0.0)
            feeB_val = transFeesB[i] / tradePriceB2[i];
          feesA[tradeIdx] = feeA_val;
          feesB[tradeIdx] = feeB_val;
          dayIndex[tradeIdx] = day + 1;
          tradeIdx++;
        }
      }

      // previous.quote.position: in R code this is sum(pnl.dsc.a[,1]) which is always 0
      // (pnl.dsc.a is set to NULL and never reassigned in the daily loop)
      // Replicate this behavior: prevQuotePos stays 0
      // prevQuotePosA = 0.0;
      // prevQuotePosB = 0.0;
    }
    // else: no crossings, positions don't change

    // Exit trade row (liquidation)
    tsA_pos[tradeIdx] = -prevQuotePosA;
    tsA_price[tradeIdx] = liquidationMidA;
    tsB_pos[tradeIdx] = -prevQuotePosB;
    tsB_price[tradeIdx] = liquidationMidB;
    feesA[tradeIdx] = 0.0;
    feesB[tradeIdx] = 0.0;
    dayIndex[tradeIdx] = day + 1;
    tradeIdx++;
  }

  // Trim to actual size
  int numTrades = tradeIdx;

  // Step 5: Final PnL assembly (inverse swap format)
  // pnl.dsc.a = cbind(trade.serie.a[,1], -trade.serie.a[,1]/trade.serie.a[,2] - fees.serie.a)
  double cumContractA = 0.0, cumBaseA = 0.0;
  double cumContractB = 0.0, cumBaseB = 0.0;

  NumericVector pnlWoMh(numTrades);

  for (int j = 0; j < numTrades; j++) {
    double posA = tsA_pos[j];
    double priceA = tsA_price[j];
    double posB = tsB_pos[j];
    double priceB = tsB_price[j];

    // Discrete PnL: contract and base currency components
    cumContractA += posA;
    if (priceA != 0.0)
      cumBaseA += -posA / priceA - feesA[j];

    cumContractB += posB;
    if (priceB != 0.0)
      cumBaseB += -posB / priceB - feesB[j];

    // Mark-to-market: contracts + base * current_price
    double pnlA = cumContractA + cumBaseA * priceA;
    double pnlB = cumContractB + cumBaseB * priceB;
    pnlWoMh[j] = pnlA + pnlB;
  }

  // Build day index vector for R
  IntegerVector dayIdx(numTrades);
  for (int j = 0; j < numTrades; j++) dayIdx[j] = dayIndex[j];

  return List::create(
    Named("pnl_wo_mh") = pnlWoMh,
    Named("daily_day_index") = dayIdx,
    Named("num_crossings") = totalCrossings
  );
}
