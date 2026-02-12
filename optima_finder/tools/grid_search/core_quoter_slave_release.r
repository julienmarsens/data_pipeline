###########################################################################################################
###########  OOS - Run -> pass absolute parameters
###########################################################################################################
run.oos.sim <- function(prices.bbo.a.b.oos,
                               dPrice,
                               quote.level,
                               normalized.trading.vector,
                               base.order.size.a,
                               base.order.size.b,
                               nc2l,
                               min.order.size.a,
                               min.order.size.b,
                               t.fees.maker.a,
                               t.fees.maker.b,
                               t.fees.taker.a,
                               t.fees.taker.b,
                               type.a,
                               type.b,
                               is.daily.hedged,
                               idx.oos.eod,
                               daily.date) {

  ###########################################################################################################
  ###########  dbg
  ###########################################################################################################
  # normalized.signal.vector  <- c(0.99903069270, -0.04401903049 )
  # margin                    <- 3.496607424
  # stepback                  <- 1.748303712
  # tick.size.a               <- 0.01
  # tick.size.b               <- 0.01
  # normalized.trading.vector <- c(0.9238795325, -0.3826834324)
  # base.order.size.a         <- 923.8795325
  # base.order.size.b         <- 382.6834324
  # nc2l                      <- 5
  # fx.a                      <- 1
  # fx.b                      <- 1
  # min.order.size.a          <- 10
  # min.order.size.b          <- 10
  # t.fees.maker.a            <- 2e-04
  # t.fees.taker.a            <- 5e-04
  # t.fees.maker.b            <- 2e-04
  # t.fees.taker.b            <- 5e-04
  # type.a                    <- "INVERSE"
  # type.b                    <- "INVERSE"
  # is.daily.hedged           <- TRUE
  
  
  ###########################################################################################################
  ###########  end-dbg 
  ###########################################################################################################
  
  ###########################################################################################################
  ###########  Core HFT statarb quoter strategy - Init 
  ###########################################################################################################
  # set as parameters
  # path2root                       <- "/Users/faria/swl/working/R"
  # path2util                       <- paste(path2root, "/util/R", sep="")
  
  ###########################################################################################################
  ###########  Load and/or install R packages and libs
  ###########################################################################################################
  # R utils
  # 20250730
  # source(paste(path2util, "/data_tools_01.r", sep="")) # data management to the sec
  # source(paste(path2util, "/data_tools.r", sep="")) # pnl calc...
  # source(paste(path2util, "/product_specs.r", sep=""))
  # end 20250730
  
  # print(paste("normalized.signal.vector:", normalized.signal.vector, sep=""))
  # print(paste("margin:", margin, sep=""))
  # print(paste("stepback:", stepback, sep=""))
  # print(paste("tick.size.a:", tick.size.a, sep=""))
  # print(paste("tick.size.b:", tick.size.b, sep=""))
  # print(paste("normalized.trading.vector:", normalized.trading.vector, sep=""))
  # print(paste("max.inventories:", max.inventories, sep=""))
  # print(paste("min.order.size.a:", min.order.size.a, sep=""))
  # print(paste("min.order.size.b:", min.order.size.b, sep=""))
  # 
  # print(paste("t.fees.maker.a:", t.fees.maker.a, sep=""))
  # print(paste("t.fees.maker.b:", t.fees.maker.b, sep=""))
  # print(paste("t.fees.taker.a:", t.fees.taker.a, sep=""))
  # print(paste("t.fees.taker.b:", t.fees.taker.b, sep=""))
  # print(paste("fx.a:", fx.a, sep=""))
  # print(paste("fx.b:", fx.b, sep=""))
  
  ###########################################################################################################
  ###########  dPrice and quote.level are now pre-computed and passed in as parameters
  ###########  (FX conversion, signal price, generateCrossing done once per outer triple)
  ###########################################################################################################

  # if no crossing at all for that rp...go to the next iteration
  if(!any(dPrice$moveTheoPriceVec!=0)) {
    print("THERE ARE NO CROSSING FOR OOS PERIOD.")
  }
  
  safe.position             <- nc2lInventoryControl(quote.level[,"bid.a"], quote.level[,"ask.a"], quote.level[,"bid.b"], quote.level[,"ask.b"],
                                                    quote.level[,"aSellLevelA"], quote.level[,"aBuyLevelA"], quote.level[,"aSellLevelB"], quote.level[,"aBuyLevelB"],
                                                    nc2l, base.order.size.a, base.order.size.b)

  # Order rounding for safe.position.a.b (legacy return field)
  clean.order.rounded       <- cbind(sign(safe.position$theOrderVecA)*floor(abs(safe.position$theOrderVecA)/min.order.size.a)*min.order.size.a,
                                     sign(safe.position$theOrderVecB)*floor(abs(safe.position$theOrderVecB)/min.order.size.b)*min.order.size.b)
  safe.position.a.b         <- cbind(cumsum(clean.order.rounded[,1]), cumsum(clean.order.rounded[,2]))

  ###########  PnL calculation (C++)
  pnl.result <- computeDailyHedgedPnL(
    quote.level[,"bid.a"], quote.level[,"ask.a"],
    quote.level[,"bid.b"], quote.level[,"ask.b"],
    quote.level[,"aSellLevelA"], quote.level[,"aBuyLevelA"],
    quote.level[,"aSellLevelB"], quote.level[,"aBuyLevelB"],
    safe.position$theOrderVecA, safe.position$theOrderVecB,
    min.order.size.a, min.order.size.b,
    t.fees.maker.a, t.fees.maker.b,
    t.fees.taker.a, t.fees.taker.b,
    as.integer(idx.oos.eod),
    is.daily.hedged)

  pnl.wo.mh.combined  <- pnl.result$pnl_wo_mh
  daily.date.serie     <- daily.date[pnl.result$daily_day_index]

  return(list(pnl.wo.mh        = pnl.wo.mh.combined,
              safe.position.a.b = safe.position.a.b,
              daily.date.serie  = daily.date.serie))
}


