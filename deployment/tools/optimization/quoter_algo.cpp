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
                      double theOrderLevelIncrementA, double theOrderLevelIncrementB) {
  
  int n = aBidSignal.size();
  float thePrecision_Prices = 1.e-5;
  
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
  
  // check for crossings, loop through all prices
  for(int i = 0; i < n; ++i) {
    
    theoPriceVec[i] = theTheorPrice; // debug
    if(((aBidSignal[i]-theTheorPrice)/theMargin)>(1-thePrecision_Prices)) {
      
      //Rprintf("#######################################################################################################\n");
      //Rprintf("[%i] Will insert UPWARD crossing now! TheoPrice: %f, bid signal: %f, margin: %f\n",
      //        i, theTheorPrice, aBidSignal[i], theMargin);

      moveTheoPriceVec[i] = ceil((aBidSignal[i]-theTheorPrice-theMargin+thePrecision_Prices*theMargin)/theStepback)*theStepback;
      theTheorPrice = theTheorPrice + moveTheoPriceVec[i];
      
      //Rprintf("[%i] moveTheoPriceVec=%f\n", i, moveTheoPriceVec[i]);
      //Rprintf("#######################################################################################################\n");
      
    } else if(((theTheorPrice-anAskSignal[i])/theMargin)>(1-thePrecision_Prices)) {
      
      //Rprintf("#######################################################################################################\n");
      //Rprintf("[%i] Will insert DOWNWARD crossing now! TheoPrice: %f, ask signal: %f, margin: %f\n",
      //       i, theTheorPrice, anAskSignal[i], theMargin);
      
      moveTheoPriceVec[i] = -ceil((theTheorPrice-anAskSignal[i]-theMargin+thePrecision_Prices*theMargin)/theStepback)*theStepback;
      theTheorPrice = theTheorPrice + moveTheoPriceVec[i];
      
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
    marginLowerX = -theNormSigA*(theMargin-theTheorPrice);
    marginLowerY = -theNormSigB*(theMargin-theTheorPrice);
    marginUpperX = theNormSigA*(theMargin+theTheorPrice);
    marginUpperY = theNormSigB*(theMargin+theTheorPrice);
    
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
                      Named("aSellLevelB") = aSellLevelB); 
}

/*** R

# investigate:

# #######################################################################################################
# [2268145] Will insert Upward crossing now! -3- aSellLevelB[i-1]: 23814.704602 <= aBidB[i]: 23818.380000, buy price at market on anAskA[i]: 23772.500000, profit: 42.204602
# #######################################################################################################
# [3421103] Will insert Downward crossing now! -4- aBuyLevelB[i-1]: 23907.735199 >= anAskB[i]: 23907.650000, sell price at market on aBidB[i]: 23907.640000, profit: -0.095199
# #######################################################################################################
# [5032719] Will insert Upward crossing now! -3- aSellLevelB[i-1]: 23682.204602 <= aBidB[i]: 23682.800000, buy price at market on anAskA[i]: 23640.000000, profit: 42.204602
# #######################################################################################################
# [5037441] Will insert Upward crossing now! -3- aSellLevelB[i-1]: 23707.194403 <= aBidB[i]: 23707.650000, buy price at market on anAskA[i]: 23640.000000, profit: 67.194403
# #######################################################################################################
# [5042530] Will insert UPWARD crossing now! -1- aSellLevelA[i-1]: 23724.255000 <= aBidA[i]: 23724.500000, buy price at market on anAskB[i]: 7.775000, profit: 67.194403
# #######################################################################################################


# idx.dbg       <- 1:700e3
# margin.inv.vector        <- c(-normalized.signal.vector[2],normalized.signal.vector[1])
# margin.inv.slope         <- margin.inv.vector[2]/margin.inv.vector[1]
# 
# dPrice        <- generateCrossing(signal.prices[idx.dbg,1], signal.prices[idx.dbg,2],
#                                   prices.bbo.a.b[idx.dbg, "bid.a"],
#                                   prices.bbo.a.b[idx.dbg, "ask.a"],
#                                   prices.bbo.a.b[idx.dbg, "bid.b"],
#                                   prices.bbo.a.b[idx.dbg, "ask.b"],
#                                   theo.price, margin, stepback,
#                                   margin.inv.vector[1], margin.inv.vector[2],
#                                   margin.inv.slope,
#                                   normalized.signal.vector[1], normalized.signal.vector[2], tick.size.a, tick.size.b)

# # check crossings and how long a quote would have become a trade
# # compute trade price on the maker and taker side
# quote.level        <- data.frame(prices.bbo.a.b[idx.dbg,],
#                                  dPrice$aSellLevelA,
#                                  dPrice$aBuyLevelA,
#                                  dPrice$aSellLevelB,
#                                  dPrice$aBuyLevelB)
#
# names(quote.level) <- c("timedate", "bid.a", "ask.a", "bid.b", "ask.b", "aSellLevelA", "aBuyLevelA", "aSellLevelB", "aBuyLevelB")

# 
# # # plot quotes
# graphics.off()
# 
# par(mfrow=c(2,1))
# # plot price a
# plot(data.source$prices.for.rp[idx.dbg+1,"bid.a"], type='l',
#      ylim=c(min(data.source$prices.for.rp[idx.dbg+1,"bid.a"], data.source$prices.for.rp[idx.dbg+1,"bid.a"],
#                 dPrice$aBuyLevelA, dPrice$aSellLevelA),
#             max(data.source$prices.for.rp[idx.dbg+1,"bid.a"], data.source$prices.for.rp[idx.dbg+1,"bid.a"],
#                 dPrice$aBuyLevelA, dPrice$aSellLevelA)))
# lines(data.source$prices.for.rp[idx.dbg,"ask.a"], col=2)
# lines(dPrice$aBuyLevelA, col=6)
# lines(dPrice$aSellLevelA, col=7)
# 
# # plot price b
# plot(data.source$prices.for.rp[idx.dbg,"bid.b"], type='l',
#      ylim=c(min(data.source$prices.for.rp[idx.dbg+1,"bid.b"], data.source$prices.for.rp[idx.dbg+1,"bid.b"],
#                 dPrice$aBuyLevelB, dPrice$aSellLevelB),
#             max(data.source$prices.for.rp[idx.dbg+1,"bid.b"], data.source$prices.for.rp[idx.dbg+1,"bid.b"],
#                 dPrice$aBuyLevelB, dPrice$aSellLevelB)))
# lines(data.source$prices.for.rp[idx.dbg+1,"ask.b"], col=2)
# lines(dPrice$aBuyLevelB, col=6)
# lines(dPrice$aSellLevelB, col=7)
# 
# which(dPrice$aBuyLevelA>=data.source$prices.for.rp[idx.dbg+1,"ask.a"])
# which(dPrice$aBuyLevelB>=data.source$prices.for.rp[idx.dbg+1,"ask.b"])
# 
# which(dPrice$aSellLevelA<=data.source$prices.for.rp[idx.dbg+1,"bid.a"])
# which(dPrice$aSellLevelB<=data.source$prices.for.rp[idx.dbg+1,"bid.b"])
# 
# plot signal price and crossing
# plot(signal.prices[idx.dbg,1], type='l',
#      ylim=c(min(c(dPrice$theTheorPriceVec-margin, dPrice$theTheorPriceVec+margin, signal.prices[idx.dbg,2])),
#             max(c(dPrice$theTheorPriceVec-margin, dPrice$theTheorPriceVec+margin, signal.prices[idx.dbg,2]))), ylab="signal prices")
# lines(signal.prices[idx.dbg,2], col=2)
# 
# lines(dPrice$theTheorPriceVec, col=3)
# lines(dPrice$theTheorPriceVec+margin, col=4, lty=2)
# lines(dPrice$theTheorPriceVec-margin, col=5, lty=2)

*/