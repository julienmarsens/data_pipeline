#include <Rcpp.h>
#include <cmath>

using namespace Rcpp;


// Legacy rm, i.e. nc2l in accordance to ccapi cpp implementation
// Inventory management function
// [[Rcpp::export]]
List nc2lInventoryControl(NumericVector aBidA, NumericVector anAskA, NumericVector aBidB, NumericVector anAskB,
                                NumericVector aSellLevelA, NumericVector aBuyLevelA, NumericVector aSellLevelB, NumericVector aBuyLevelB, 
                                int nc2l, double aBaseOrderSizeA, double aBaseOrderSizeB) {
  
  int n = aSellLevelA.size();
  float thePrecision_Prices = 1.e-5;
  
  double maxInventoryA = nc2l * aBaseOrderSizeA;
  double maxInventoryB = nc2l * aBaseOrderSizeB;  
  
  //Rprintf("max inventoryA : %f, max inventoryB : %f\n", maxInventoryA, maxInventoryB);
  
  // create vectors
  NumericVector theOrderVecA(n); 
  NumericVector theOrderVecB(n);
  
  NumericVector thePositionVecA(n); 
  NumericVector thePositionVecB(n);
  
  // manage inventory across all prices
  for(int i = 0; i < n; ++i) {
    
    // init orders at t=0
    if(i==0) {
      theOrderVecA[i] = 0.0;
      theOrderVecB[i] = 0.0;
      
      // init positions
      thePositionVecA[i] = 0.0;
      thePositionVecB[i] = 0.0;
      
    } else{
      // carry on inventory over time
      thePositionVecA[i] = thePositionVecA[i-1];
      thePositionVecB[i] = thePositionVecB[i-1];
      
      // DEBUG
      //if(i>50929 && i<50932) {
      if(i< -1) {
        Rprintf("#######################################################################################################\n");
        Rprintf("[%i] aSellLevelA[i-1]:%.10f, ?<= aBidA[i]: %.10f\n", i, aSellLevelA[i-1], aBidA[i]);
        if((aSellLevelA[i-1] - aBidA[i]) < thePrecision_Prices) {
          Rprintf("###########    crossing!!!!!!");
        }
        // if(aSellLevelA[i-1] == aBidA[i]) {
        //   Rprintf("###########    crossing 1 !!!!!!");
        // }
        // if(aSellLevelA[i-1] >= aBidA[i]) {
        //   Rprintf("###########    crossing 2 !!!!!!");
        // }
      }
      
      // END DEBUG
      
      //if(aSellLevelA[i-1] <= aBidA[i]) {
      if((aSellLevelA[i-1] - aBidA[i]) <= thePrecision_Prices) {
        // Upward crossing
        //Rprintf("#######################################################################################################\n");
        //Rprintf("[%i] Will insert Upward crossing now! -1- aSellLevelA[i-1]: %f <= aBidA[i]: %f, buy price at market on anAskB[i]: %f, profit: %f\n",
        //          i, aSellLevelA[i-1], aBidA[i], aSellLevelA[i-1]-anAskB[i]);
        
        // compute the position accordingly if risk limit is not breached enter new trade; protocol -> if upward crossing add a crossing
        if(maxInventoryA >= abs(thePositionVecA[i-1] -aBaseOrderSizeA) && maxInventoryB >= abs(thePositionVecB[i-1] + aBaseOrderSizeB)) {  
          // compute the order size
          theOrderVecA[i] = -aBaseOrderSizeA;
          theOrderVecB[i] = aBaseOrderSizeB;
          
          thePositionVecA[i] = thePositionVecA[i-1] + theOrderVecA[i];
          thePositionVecB[i] = thePositionVecB[i-1] + theOrderVecB[i];

          //Rprintf("[%i] Will insert Upward crossing now! orderA: %f, orderB: %f, previous positionA: %f, previous positionB: %f, current positionA: %f, current positionB: %f \n", 
          //        i, -aBaseOrderSizeA, aBaseOrderSizeB, thePositionVecA[i-1], thePositionVecB[i-1], thePositionVecA[i], thePositionVecB[i]);
        }
      }
      
      //if(aBuyLevelA[i-1] >= anAskA[i]) {
      if((aBuyLevelA[i-1] - anAskA[i]) >= -thePrecision_Prices) {
        // Downward crossing
        //Rprintf("#######################################################################################################\n");
        //Rprintf("[%i] Will insert Downward crossing now! -2- aBuyLevelA[i-1]: %f >= anAskA[i]: %f, sell price at market on aBidB[i]: %f, profit: %f\n",
        //        i, aBuyLevelA[i-1], anAskA[i], aBidB[i], aBidB[i]-aBuyLevelA[i-1]);
        
        // compute the position accordingly
        if(maxInventoryA >= abs(thePositionVecA[i-1] + aBaseOrderSizeA) && maxInventoryB >= abs(thePositionVecB[i-1] - aBaseOrderSizeB)) {
          // compute the order size
          theOrderVecA[i] = aBaseOrderSizeA;
          theOrderVecB[i] = -aBaseOrderSizeB;
          
          thePositionVecA[i] = thePositionVecA[i-1] + theOrderVecA[i];
          thePositionVecB[i] = thePositionVecB[i-1] + theOrderVecB[i];
          
          // Rprintf("[%i] Will insert Downward crossing now! orderA: %f, orderB: %f, previous positionA: %f, previous positionB: %f, current positionA: %f, current positionB: %f \n", 
          //        i, -aBaseOrderSizeA, aBaseOrderSizeB, thePositionVecA[i-1], thePositionVecB[i-1], thePositionVecA[i], thePositionVecB[i]);
        }
      }
      
      //if(aSellLevelB[i-1] <= aBidB[i]) {
      if((aSellLevelB[i-1] - aBidB[i]) <= thePrecision_Prices) {
        // Upward crossing
        //Rprintf("#######################################################################################################\n");
        //Rprintf("[%i] Will insert Downward crossing now! -3- aSellLevelB[i-1]: %f <= aBidB[i]: %f, buy price at market on anAskA[i]: %f, profit: %f\n",
        //          i, aSellLevelB[i-1], aBidB[i], anAskA[i], aSellLevelB[i-1]-anAskA[i]);
        
        // compute the position accordingly
        if(maxInventoryA >= abs(thePositionVecA[i-1] + aBaseOrderSizeA) && maxInventoryB >= abs(thePositionVecB[i-1] - aBaseOrderSizeB)) {
          // compute the order size
          theOrderVecA[i] = aBaseOrderSizeA;
          theOrderVecB[i] = -aBaseOrderSizeB;
          
          thePositionVecA[i] = thePositionVecA[i-1] + theOrderVecA[i];
          thePositionVecB[i] = thePositionVecB[i-1] + theOrderVecB[i];
          
          //Rprintf("[%i] Will insert Downward crossing now! orderA: %f, orderB: %f, previous positionA: %f, previous positionB: %f, current positionA: %f, current positionB: %f \n", 
          //        i, -aBaseOrderSizeA, aBaseOrderSizeB, thePositionVecA[i-1], thePositionVecB[i-1], thePositionVecA[i], thePositionVecB[i]);
        }
      }
      
      //if(aBuyLevelB[i-1] >= anAskB[i]) {
      if((aBuyLevelB[i-1] - anAskB[i]) >= -thePrecision_Prices) {
        // Upward crossing
        //Rprintf("#######################################################################################################\n");
        //Rprintf("[%i] Will insert Upward crossing now! -4- aBuyLevelB[i-1]: %f >= anAskB[i]: %f, sell price at market on aBidB[i]: %f, profit: %f\n",
        //         i, aBuyLevelB[i-1], anAskB[i], aBidB[i], aBidB[i]-aBuyLevelB[i-1]);
        
        // compute the position accordingly
        if(maxInventoryA >= abs(thePositionVecA[i-1] - aBaseOrderSizeA) && maxInventoryB >= abs(thePositionVecB[i-1] + aBaseOrderSizeB)) {
          // compute the order size
          theOrderVecA[i] = -aBaseOrderSizeA;
          theOrderVecB[i] = aBaseOrderSizeB;
          
          thePositionVecA[i] = thePositionVecA[i-1] + theOrderVecA[i];
          thePositionVecB[i] = thePositionVecB[i-1] + theOrderVecB[i];
          
          // Rprintf("[%i] Will insert Upward crossing now! orderA: %f, orderB: %f, previous positionA: %f, previous positionB: %f, current positionA: %f, current positionB: %f \n", 
          //        i, -aBaseOrderSizeA, aBaseOrderSizeB, thePositionVecA[i-1], thePositionVecB[i-1], thePositionVecA[i], thePositionVecB[i]);
        }
      }
    }
  }
  
  return List::create(Named("theOrderVecA") = theOrderVecA,
                      Named("theOrderVecB") = theOrderVecB,
                      Named("thePositionVecA") = thePositionVecA,
                      Named("thePositionVecB") = thePositionVecB); 
}



/*** R

# 
# output from quoter
# quote.level        <- data.frame(data.source$prices.for.rp[idx.dbg,],
#                                  dPrice$aSellLevelA,
#                                  dPrice$aBuyLevelA,
#                                  dPrice$aSellLevelB,
#                                  dPrice$aBuyLevelB)
# names(quote.level) <- c("timedate", "bid.a", "ask.a", "bid.b", "ask.b", "aSellLevelA", "aBuyLevelA", "aSellLevelB", "aBuyLevelB")
# 
# base.order.size.a <- 100
# base.order.size.b <- 100
# 
# # base.order.size.a <- order.size[1]
# # base.order.size.b <- order.size[2]
# 
# nc2l    <- 10
# nc2lic  <- nc2lInventoryControl(quote.level[,"bid.a"], quote.level[,"ask.a"], quote.level[,"bid.b"], quote.level[,"ask.b"],
#                                 quote.level[,"aSellLevelA"], quote.level[,"aBuyLevelA"], quote.level[,"aSellLevelB"], quote.level[,"aBuyLevelB"],
#                                 nc2l, base.order.size.a, base.order.size.b)
# 
# safe.position.a.b      <- cbind(nc2lic$thePositionVecA, nc2lic$thePositionVecB)
# clean.order.rounded        <- rbind(c(0, 0), diff(safe.position.a.b))
# plot(safe.position.a.b[,1], type='l', ylim=c(-700,700))
# lines(safe.position.a.b[,2], col=2)
# idx.xing                   <- clean.order.rounded[,2] !=0
# which(idx.xing)

# 27 2024-03-01T20:08:33.000000000Z      prodB       17              84.650                 35 SELL        1    40855 2024-03-01T20:08:32.000Z_SELL 0.0008269344359       LTC            105
# 28 2024-03-01T20:08:33.000000000Z      prodA       11               8.399                 35  BUY        0    51451  2024-03-01T20:08:33.000Z_BUY 0.0208358137870       DOT            140
# 29 2024-03-01T22:53:04.000000000Z      prodA       18               8.665                 35 SELL        1    55991 2024-03-01T22:52:46.000Z_SELL 0.0080784766301       DOT            105
# 30 2024-03-01T22:53:04.000000000Z      prodB       19              84.930                 35  BUY        0    44464  2024-03-01T22:53:04.000Z_BUY 0.0020605204286       LTC            105

# idx.a <- which(quote.level[,"ask.a"] == 8.665)
# which(quote.level[idx.a,"ask.b"]==84.930)
# 
# 50926 50927 50930 51449 51477
# 
# quote.level[50926:50931,]
# timedate             bid.a ask.a bid.b ask.b aSellLevelA aBuyLevelA aSellLevelB aBuyLevelB
# 50926 1709333563905 8.664 8.665 84.93 84.94       8.668      8.249       86.94      84.92
# 50927 1709333566620 8.664 8.665 84.92 84.93       8.665      8.247       86.94      84.92
# 50928 1709333567865 8.662 8.663 84.92 84.93       8.665      8.247       86.93      84.91

# 50929 1709333572582 8.663 8.664 84.92 84.93       8.665      8.247       86.93      84.92
# 50930 1709333582809 8.664 8.665 84.92 84.93       8.665      8.247       86.94      84.92
# 50931 1709333584925 8.665 8.666 84.92 84.93       8.873      8.455       85.94      83.93

# #######################################################################################################
# [959] Will insert Downward crossing now! -3- aSellLevelB[i-1]: 81.240000 <= aBidB[i]: 81.240000, buy price at market on anAskA[i]: 8.312000, profit: 72.928000
# #######################################################################################################
# [1780] Will insert Downward crossing now! -3- aSellLevelB[i-1]: 82.330000 <= aBidB[i]: 82.350000, buy price at market on anAskA[i]: 8.331000, profit: 73.999000
# #######################################################################################################
# [2144] Will insert Downward crossing now! -2- aBuyLevelA[i-1]: 8.349000 >= anAskA[i]: 8.349000, sell price at market on aBidB[i]: 83.410000, profit: 75.061000
# #######################################################################################################
# [2671] Will insert Upward crossing now! -4- aBuyLevelB[i-1]: 82.610000 >= anAskB[i]: 82.600000, sell price at market on aBidB[i]: 82.590000, profit: -0.020000
# #######################################################################################################
# [4779] Will insert Downward crossing now! -2- aBuyLevelA[i-1]: 8.361000 >= anAskA[i]: 8.360000, sell price at market on aBidB[i]: 83.470000, profit: 75.109000
# #######################################################################################################
# [9219] Will insert Downward crossing now! -2- aBuyLevelA[i-1]: 8.340000 >= anAskA[i]: 8.340000, sell price at market on aBidB[i]: 84.370000, profit: 76.030000
# #######################################################################################################
# [12807] Will insert Upward crossing now! -4- aBuyLevelB[i-1]: 83.570000 >= anAskB[i]: 83.560000, sell price at market on aBidB[i]: 83.550000, profit: -0.020000
# #######################################################################################################
# [14704] Will insert Downward crossing now! -3- aSellLevelB[i-1]: 84.870000 <= aBidB[i]: 84.870000, buy price at market on anAskA[i]: 8.443000, profit: 76.427000
# #######################################################################################################
# [15001] Will insert Downward crossing now! -3- aSellLevelB[i-1]: 85.940000 <= aBidB[i]: 85.940000, buy price at market on anAskA[i]: 8.459000, profit: 77.481000
# #######################################################################################################
# [17189] Will insert Upward crossing now! -4- aBuyLevelB[i-1]: 84.830000 >= anAskB[i]: 84.830000, sell price at market on aBidB[i]: 84.820000, profit: -0.010000
# #######################################################################################################
# [18692] Will insert Downward crossing now! -3- aSellLevelB[i-1]: 85.760000 <= aBidB[i]: 85.770000, buy price at market on anAskA[i]: 8.421000, profit: 77.339000
# #######################################################################################################
# [21265] Will insert Upward crossing now! -4- aBuyLevelB[i-1]: 84.530000 >= anAskB[i]: 84.530000, sell price at market on aBidB[i]: 84.520000, profit: -0.010000
# #######################################################################################################
# [32421] Will insert Upward crossing now! -4- aBuyLevelB[i-1]: 84.140000 >= anAskB[i]: 84.140000, sell price at market on aBidB[i]: 84.120000, profit: -0.020000
# #######################################################################################################
# [46809] Will insert Downward crossing now! -3- aSellLevelB[i-1]: 84.650000 <= aBidB[i]: 84.650000, buy price at market on anAskA[i]: 8.399000, profit: 76.251000
# #######################################################################################################
# [50930] Will insert Upward crossing now! -1- aSellLevelA[i-1]: 8.665000 <= aBidA[i]: 8.665000, buy price at market on anAskB[i]: -76.265000, profit: 76.251000

#ptrade1


*/
  