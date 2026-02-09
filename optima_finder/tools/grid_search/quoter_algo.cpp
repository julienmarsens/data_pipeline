#include <Rcpp.h>
#include <cmath>

using namespace Rcpp;

// the following implements the original version of the crossing algorithm.
// to be called every day with potentially resetting the mid-price/theoretical price
// [[Rcpp::export]]
List generateCrossing(NumericVector aBidSignal, NumericVector anAskSignal,
                      NumericVector aBidA, NumericVector anAskA, NumericVector aBidB, NumericVector anAskB,
                      double theTheorPrice, double theMargin, double theStepback,
                      double theMarginInvA, double theMarginInvB,
                      double theMarginInvSlope,
                      double theNormSigA, double theNormSigB,
                      double theOrderLevelIncrementA, double theOrderLevelIncrementB,
                      bool enableSkew = false) {

  int n = aBidSignal.size();
  float thePrecision_Prices = 1.e-5;

  // Skew mechanism variables
  double skewUpperBound = 1.0;
  double skewLowerBound = 1.0;
  int nbTickSinceUpCrossing = 0;
  int nbTickSinceDownCrossing = 0;
  
  double marginLowerX = 0.0;
  double marginLowerY = 0.0;
  double marginUpperX = 0.0;
  double marginUpperY = 0.0;
  
  double yInterceptMarginLower = 0.0;
  double yInterceptMarginUpper = 0.0;
  
  // create target vectors of the same size
  NumericVector moveTheoPriceVec(n);     
  NumericVector theoPriceVec(n); // debug
  
  NumericVector aBuyLevelA(n); // level of the buy a quote
  NumericVector aSellLevelA(n);
  
  NumericVector aSellLevelB(n); // level of the buy a quote
  NumericVector aBuyLevelB(n);

  // Skew tracking vectors
  NumericVector skewUpperVec(n, 1.0);  // initialized to 1.0
  NumericVector skewLowerVec(n, 1.0);  // initialized to 1.0
  
  // check for crossings, loop through all prices
  for(int i = 0; i < n; ++i) {

    // Increment tick counters for skew mechanism
    if (enableSkew) {
      nbTickSinceUpCrossing++;
      nbTickSinceDownCrossing++;
    }

    theoPriceVec[i] = theTheorPrice; // debug

    // Apply skew multipliers to margins
    double effectiveMarginUpper = theMargin * skewUpperBound;
    double effectiveMarginLower = theMargin * skewLowerBound;

    // UPWARD crossing detection (using skewed upper margin)
    if(((aBidSignal[i]-theTheorPrice)/effectiveMarginUpper)>(1-thePrecision_Prices)) {

      //Rprintf("#######################################################################################################\n");
      //Rprintf("[%i] Will insert UPWARD crossing now! TheoPrice: %f, bid signal: %f, margin: %f\n",
      //        i, theTheorPrice, aBidSignal[i], effectiveMarginUpper);

      moveTheoPriceVec[i] = ceil((aBidSignal[i]-theTheorPrice-effectiveMarginUpper+thePrecision_Prices*effectiveMarginUpper)/theStepback)*theStepback;
      theTheorPrice = theTheorPrice + moveTheoPriceVec[i];

      // Update skew bounds after upward crossing
      if (enableSkew) {
        if (nbTickSinceUpCrossing <= 90) {
          skewUpperBound = 2.0;
        }
        if (nbTickSinceUpCrossing <= 60) {
          skewUpperBound = 3.0;
        }
        if (nbTickSinceUpCrossing <= 30) {
          skewUpperBound = 4.0;
        }

        // Counter-balance: reduce opposite side skew
        skewLowerBound = std::max(1.0, skewLowerBound - 1.0);

        // Reset counter after crossing
        nbTickSinceUpCrossing = 0;
      }

      //Rprintf("[%i] moveTheoPriceVec=%f\n", i, moveTheoPriceVec[i]);
      //Rprintf("#######################################################################################################\n");

    } else if(((theTheorPrice-anAskSignal[i])/effectiveMarginLower)>(1-thePrecision_Prices)) {

      //Rprintf("#######################################################################################################\n");
      //Rprintf("[%i] Will insert DOWNWARD crossing now! TheoPrice: %f, ask signal: %f, margin: %f\n",
      //       i, theTheorPrice, anAskSignal[i], effectiveMarginLower);

      moveTheoPriceVec[i] = -ceil((theTheorPrice-anAskSignal[i]-effectiveMarginLower+thePrecision_Prices*effectiveMarginLower)/theStepback)*theStepback;
      theTheorPrice = theTheorPrice + moveTheoPriceVec[i];

      // Update skew bounds after downward crossing
      if (enableSkew) {
        if (nbTickSinceDownCrossing <= 90) {
          skewLowerBound = 2.0;
        }
        if (nbTickSinceDownCrossing <= 60) {
          skewLowerBound = 3.0;
        }
        if (nbTickSinceDownCrossing <= 30) {
          skewLowerBound = 4.0;
        }

        // Counter-balance: reduce opposite side skew
        skewUpperBound = std::max(1.0, skewUpperBound - 1.0);

        // Reset counter after crossing
        nbTickSinceDownCrossing = 0;
      }

      //Rprintf("[%i] moveTheoPriceVec=%f\n", i, moveTheoPriceVec[i]);
      //Rprintf("#######################################################################################################\n");
    }
    
    // quoter logic
    // quoter internal parameters
    // R
    // margin.lower.xy     <- -normalized.signal.vector * margin/2-theoretical.price
    // margin.upper.xy     <- normalized.signal.vector * margin/2+theoretical.price

    // y.intercept.margin.lower <- margin.lower.xy[2]-margin.inv.vector[2]/margin.inv.vector[1]*margin.lower.xy[1]
    // y.intercept.margin.upper <- margin.upper.xy[2]-margin.inv.vector[2]/margin.inv.vector[1]*margin.upper.xy[1]

    // # freeze leg A and quote leg B
    // sell.quote.level.b       <- margin.inv.slope*ask.a + y.intercept.margin.lower
    // buy.quote.level.b        <- margin.inv.slope*bid.a + y.intercept.margin.upper

    // # freeze leg B and quote leg A
    // sell.quote.level.a       <- (ask.b - y.intercept.margin.upper)/margin.inv.slope
    // buy.quote.level.a        <- (bid.b - y.intercept.margin.lower)/margin.inv.slope

    // C++
    // Update effective margins for quote levels (skew bounds may have changed after crossing)
    effectiveMarginUpper = theMargin * skewUpperBound;
    effectiveMarginLower = theMargin * skewLowerBound;

    marginLowerX = -theNormSigA*(effectiveMarginLower-theTheorPrice);
    marginLowerY = -theNormSigB*(effectiveMarginLower-theTheorPrice);
    marginUpperX = theNormSigA*(effectiveMarginUpper+theTheorPrice);
    marginUpperY = theNormSigB*(effectiveMarginUpper+theTheorPrice);
    
    yInterceptMarginLower = marginLowerY-theMarginInvB/theMarginInvA*marginLowerX;
    yInterceptMarginUpper = marginUpperY-theMarginInvB/theMarginInvA*marginUpperX;

    // version 01, introduce rounding to order level increment
    //theOrderLevelIncrementA
    // without rounding
    // freeze leg A and quote leg B  
    aSellLevelB[i] = ceil((theMarginInvSlope*anAskA[i]+yInterceptMarginLower)/theOrderLevelIncrementB)*theOrderLevelIncrementB;
    aBuyLevelB[i] = floor((theMarginInvSlope*aBidA[i]+yInterceptMarginUpper)/theOrderLevelIncrementB)*theOrderLevelIncrementB;
    
    //aSellLevelB[i] = theMarginInvSlope*anAskA[i]+yInterceptMarginLower;
    //aBuyLevelB[i] = theMarginInvSlope*aBidA[i]+yInterceptMarginUpper;
    
    // freeze leg B and quote leg A
    aSellLevelA[i] = ceil(((anAskB[i]-yInterceptMarginUpper)/theMarginInvSlope)/theOrderLevelIncrementA)*theOrderLevelIncrementA;
    aBuyLevelA[i] = floor(((aBidB[i]-yInterceptMarginLower)/theMarginInvSlope)/theOrderLevelIncrementA)*theOrderLevelIncrementA;
    
    //aSellLevelA[i] = (anAskB[i]-yInterceptMarginUpper)/theMarginInvSlope;
    //aBuyLevelA[i] = (aBidB[i]-yInterceptMarginLower)/theMarginInvSlope;
    
    // Record skew state at this tick
    skewUpperVec[i] = skewUpperBound;
    skewLowerVec[i] = skewLowerBound;

    //if(moveTheoPriceVec[i]!=0){
      //11824003
    if(i< -1 ) {
    //if(i>11822731 & i<11822734) {
      
      Rprintf("#######################################################################################################\n");
      Rprintf("[%i] moveTheoPriceVec=%f\n", i, moveTheoPriceVec[i]);
      Rprintf("[%i] aSellLevelB[i]: %f, aBuyLevelB[i]: %f, aSellLevelA[i]: %f, aBuyLevelA[i]: %f, aBidA[i]: %f, anAskA[i]: %f, aBidB[i]: %f, anAskB[i]: %f\n",
              i, aSellLevelB[i], aBuyLevelB[i], aSellLevelA[i], aBuyLevelA[i], aBidA[i],anAskA[i],aBidB[i],anAskB[i]);
      
      Rprintf("[%i] Recompute margin! margin lower x: %f, margin lower y: %f, margin upper x: %f, margin upper y: %f, yInterceptMarginLower: %f, yInterceptMarginUpper: %f\n",
              i, marginLowerX, marginLowerY, marginUpperX, marginUpperY, yInterceptMarginLower, yInterceptMarginUpper);
      
      Rprintf("######################Upward condition\n");
      Rprintf("[%i] Upward crossing condition is %f > %f\n", i, (aBidSignal[i]-theTheorPrice)/theMargin, (1-thePrecision_Prices));
      //(aBidSignal[i]-theTheorPrice)/theMargin)>(1-thePrecision_Prices)
      
      Rprintf("######################Downward condition\n");
      Rprintf("[%i] Downward crossing condition is %f > %f\n", i, (theTheorPrice-anAskSignal[i])/theMargin, (1-thePrecision_Prices));
      //(theTheorPrice-anAskSignal[i])/theMargin)>(1-thePrecision_Prices)
    }
  }
  
  return List::create(Named("moveTheoPriceVec") = moveTheoPriceVec,
                      Named("theTheorPrice") = theTheorPrice,
                      Named("theTheorPriceVec") = theoPriceVec,
                      Named("aBuyLevelA") = aBuyLevelA,
                      Named("aBuyLevelB") = aBuyLevelB,
                      Named("aSellLevelA") = aSellLevelA,
                      Named("aSellLevelB") = aSellLevelB,
                      Named("skewUpperVec") = skewUpperVec,
                      Named("skewLowerVec") = skewLowerVec);
}
