###########################################################################################################
###########  OOS - Run -> pass absolute parameters
###########################################################################################################
run.oos.sim <- function(prices.bbo.a.b.oos, 
                               normalized.signal.vector, 
                               margin, 
                               stepback,
                               tick.size.a, 
                               tick.size.b,
                               normalized.trading.vector,
                               base.order.size.a,
                               base.order.size.b,
                               nc2l,
                               fx.a,
                               fx.b,
                               min.order.size.a,
                               min.order.size.b,
                               t.fees.maker.a,
                               t.fees.maker.b,
                               t.fees.taker.a,
                               t.fees.taker.b,
                               type.a,
                               type.b,
                               # path2root,
                               # path2util,
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
  ###########  In case the products are not quoted in USD -> deal with FX rates conversion
  ###########################################################################################################
  prices.bbo.a.b.oos[, c("bid.a", "ask.a", "bid.b", "ask.b")]       <- t(t(prices.bbo.a.b.oos[, c("bid.a", "ask.a", "bid.b", "ask.b")]) * c(fx.a,fx.a,fx.b,fx.b))
  
  ###########################################################################################################
  ###########  Compute signal price based on signal angle provided by the is period
  ###########################################################################################################
  # rem don't consider max bid-ask filter (ref. maker vs taker model) 
  # if(max.bid.ask.filter) {
  #   price.2.use <- prices.bbo.a.b.filter
  # } else {
  #   price.2.use <- prices.bbo.a.b
  # }
  
  price.2.use <- prices.bbo.a.b.oos
  
  # determine whether positively or negatively correlated pair
  #if(cov.mat[1,2]<0) {
  if(FALSE) {
    # positive base direction
    signal.prices            <- cbind(price.2.use[, "bid.a"] * normalized.signal.vector[1] +
                                        price.2.use[, "bid.b"] * normalized.signal.vector[2],
                                      price.2.use[, "ask.a"] * normalized.signal.vector[1] +
                                        price.2.use[, "ask.b"] * normalized.signal.vector[2])
  } else {
    # negative base direction
    signal.prices            <- cbind(price.2.use[, "bid.a"] * normalized.signal.vector[1] +
                                        price.2.use[, "ask.b"] * normalized.signal.vector[2],
                                      price.2.use[, "ask.a"] * normalized.signal.vector[1] +
                                        price.2.use[, "bid.b"] * normalized.signal.vector[2])
  }
  
  ###########################################################################################################
  ###########  Call crossing algorithm to get triggers (C++ code to speed up things)
  ###########################################################################################################
  theo.price               <- (signal.prices[1,1]+signal.prices[1,2])/2 # theoretical price at t=0
  
  ###########################################################################################################
  ###########  Quoter 
  ###########################################################################################################
  margin.inv.vector        <- c(-normalized.signal.vector[2],normalized.signal.vector[1])
  margin.inv.slope         <- margin.inv.vector[2]/margin.inv.vector[1]
  
  dPrice        <- generateCrossing(signal.prices[,1], signal.prices[,2],
                                    price.2.use[, "bid.a"],
                                    price.2.use[, "ask.a"],
                                    price.2.use[, "bid.b"],
                                    price.2.use[, "ask.b"],
                                    theo.price, margin, stepback,
                                    margin.inv.vector[1], margin.inv.vector[2],
                                    margin.inv.slope,
                                    normalized.signal.vector[1], normalized.signal.vector[2],
                                    tick.size.a, tick.size.b)
  
  # check crossings and how long a quote would have become a trade
  # compute trade price on the maker and taker side
  quote.level        <- data.frame(price.2.use,
                                   dPrice$aSellLevelA,
                                   dPrice$aBuyLevelA,
                                   dPrice$aSellLevelB,
                                   dPrice$aBuyLevelB)
  
  names(quote.level) <- c("timedate", "bid.a", "ask.a", "bid.b", "ask.b", "aSellLevelA", "aBuyLevelA", "aSellLevelB", "aBuyLevelB")
  
  # JF: TO BE UPDATED 20250801
  # Consider the idx of crossing == movedp + 1
  # idx.occurence      <- dPrice$crossingIdx !=0
  # if(!any(idx.occurence)) {
  #   print("THERE ARE NO CROSSING FOR OOS PERIOD.")
  # }
  # if no crossing at all for that rp...go to the next iteration
  if(!any(dPrice$moveTheoPriceVec!=0)) {
    print("THERE ARE NO CROSSING FOR OOS PERIOD.")
  }
  
  safe.position             <- nc2lInventoryControl(quote.level[,"bid.a"], quote.level[,"ask.a"], quote.level[,"bid.b"], quote.level[,"ask.b"],
                                                    quote.level[,"aSellLevelA"], quote.level[,"aBuyLevelA"], quote.level[,"aSellLevelB"], quote.level[,"aBuyLevelB"],
                                                    nc2l, base.order.size.a, base.order.size.b)
  
  # constrains to min order size
  clean.order.rounded       <- cbind(sign(safe.position$theOrderVecA)*floor(abs(safe.position$theOrderVecA)/min.order.size.a)*min.order.size.a,
                                     sign(safe.position$theOrderVecB)*floor(abs(safe.position$theOrderVecB)/min.order.size.b)*min.order.size.b)
  
  safe.position.a.b         <- cbind(cumsum(clean.order.rounded[,1]), cumsum(clean.order.rounded[,2]))
  
  # necessary for pnl calculation -> crossing index
  idx.xing                   <- clean.order.rounded[,1] !=0 | clean.order.rounded[,2] !=0
  
  ###########################################################################################################
  ###########  Don't scale order size as we cannot further anticipate the next move 
  ###########  LEGACY VALIDATION/ORDER SCALING
  ###########################################################################################################
  # v2.0 
  # EXEC SUMMARY: a) the move of theo price will occur at the first crossing and applied from the second crossing.
  #               b) the first move of theo price at will be triggered due to the first crossing, that move is a
  #                  proxy of the first crossing description (which direction the first crossing took place).
  #               c) therefore that first move will be used twice; once to qualify the direction of the first crossing,
  #                  as well as the direction AND scaling of the second crossing.
  #               d) thereafter the move of the theo price will be applied for the ALL the next crossings to qualify
  #                  the direction AND scaling.
  
  # raw.order.org                          <- cbind(dPrice$crossingIdx*0, dPrice$crossingIdx*0)
  # 
  # serie.length                           <- length(dPrice$crossingIdx[dPrice$crossingIdx!=0])
  # raw.order.org[dPrice$crossingIdx!=0,]  <- rbind(-dPrice$crossingIdx[dPrice$crossingIdx!=0][1]*typical.trade,
  #                                                 cbind(-Q*dPrice$crossingIdx[dPrice$crossingIdx!=0][-1]*abs(dPrice$moveTheoPriceVec[dPrice$moveTheoPriceVec!=0][-serie.length]),
  #                                                       -Q*dPrice$crossingIdx[dPrice$crossingIdx!=0][-1]*abs(dPrice$moveTheoPriceVec[dPrice$moveTheoPriceVec!=0][-serie.length]))%*%diag(normalized.trading.vector))

  ###########################################################################################################
  ###########  PnL calculation
  ###########################################################################################################
  # idx.1                   <- quote.level[-nrow(quote.level),"aSellLevelA"] <= quote.level[-1,"bid.a"]
  # idx.2                   <- quote.level[-nrow(quote.level),"aBuyLevelA"] >= quote.level[-1,"ask.a"]
  # idx.3                   <- quote.level[-nrow(quote.level),"aSellLevelB"] <= quote.level[-1,"bid.b"]
  # idx.4                   <- quote.level[-nrow(quote.level),"aBuyLevelB"] >= quote.level[-1,"ask.b"]
  
  precision.prices        <-  1.e-5
  idx.1                   <- quote.level[-nrow(quote.level),"aSellLevelA"] - quote.level[-1,"bid.a"] <= precision.prices
  idx.2                   <- quote.level[-nrow(quote.level),"aBuyLevelA"] - quote.level[-1,"ask.a"] >= -precision.prices
  idx.3                   <- quote.level[-nrow(quote.level),"aSellLevelB"] - quote.level[-1,"bid.b"] <= precision.prices
  idx.4                   <- quote.level[-nrow(quote.level),"aBuyLevelB"] - quote.level[-1,"ask.b"] >= -precision.prices
  
  # which(idx.1)
  # which(idx.2)
  # which(idx.3)
  # which(idx.4)
  # 
  # c(which(idx.1), which(idx.2), which(idx.3), which(idx.4))
  
  # in case the riskwall is reached...to trades occur
  # rem lag by one to consider crossing levels from t-1 to t...
  # idx.1                   <- quote.level[-nrow(quote.level),"aSellLevelA"] <= quote.level[-1,"bid.a"] & idx.xing[-1]
  # idx.2                   <- quote.level[-nrow(quote.level),"aBuyLevelA"] >= quote.level[-1,"ask.a"] & idx.xing[-1]
  # idx.3                   <- quote.level[-nrow(quote.level),"aSellLevelB"] <= quote.level[-1,"bid.b"] & idx.xing[-1]
  # idx.4                   <- quote.level[-nrow(quote.level),"aBuyLevelB"] >= quote.level[-1,"ask.b"] & idx.xing[-1]
  
  trade.price.raw         <- cbind(rep(0, time=nrow(safe.position.a.b)),
                                   rep(0, time=nrow(safe.position.a.b)),
                                   rep(0, time=nrow(safe.position.a.b)),
                                   rep(0, time=nrow(safe.position.a.b)))
  
  trade.price.raw[which(idx.1)+1,] <- cbind(quote.level[which(idx.1),"aSellLevelA"],
                                            quote.level[which(idx.1),"aSellLevelA"],
                                            quote.level[which(idx.1)+1,"ask.b"],
                                            quote.level[which(idx.1)+1,"ask.b"])
  trade.price.raw[which(idx.2)+1,] <- cbind(quote.level[which(idx.2),"aBuyLevelA"],
                                            quote.level[which(idx.2),"aBuyLevelA"],
                                            quote.level[which(idx.2)+1,"bid.b"],
                                            quote.level[which(idx.2)+1,"bid.b"])
  trade.price.raw[which(idx.3)+1,] <- cbind(quote.level[which(idx.3)+1,"ask.a"],
                                            quote.level[which(idx.3)+1,"ask.a"],
                                            quote.level[which(idx.3),"aSellLevelB"],
                                            quote.level[which(idx.3),"aSellLevelB"])
  trade.price.raw[which(idx.4)+1,] <- cbind(quote.level[which(idx.4)+1,"bid.a"],
                                            quote.level[which(idx.4)+1,"bid.a"],
                                            quote.level[which(idx.4),"aBuyLevelB"],
                                            quote.level[which(idx.4),"aBuyLevelB"])
  
  trade.price                      <- data.frame(prices.bbo.a.b.oos[,1], trade.price.raw)
  
  # compute the transaction fees. -> retrieved dynamically from product_specs.r file
  # see master script for details
  transaction.fees                  <- cbind(rep(0, time=nrow(safe.position.a.b)),
                                             rep(0, time=nrow(safe.position.a.b)))
  
  order.fees                   <- clean.order.rounded *0
  order.fees[which(idx.1),1]   <- clean.order.rounded[which(idx.1)+1,1]
  order.fees[which(idx.1)+1,2] <- clean.order.rounded[which(idx.1)+1,2]
  
  order.fees[which(idx.2),1]   <- clean.order.rounded[which(idx.2)+1,1]
  order.fees[which(idx.2)+1,2] <- clean.order.rounded[which(idx.2)+1,2]
  
  order.fees[which(idx.3)+1,1] <- clean.order.rounded[which(idx.3)+1,1]
  order.fees[which(idx.3),2]   <- clean.order.rounded[which(idx.3)+1,2]
  
  order.fees[which(idx.4)+1,1] <- clean.order.rounded[which(idx.4)+1,1]
  order.fees[which(idx.4),2]   <- clean.order.rounded[which(idx.4)+1,2]
  
  
  transaction.fees[which(idx.1)+1,] <- cbind(abs(order.fees[which(idx.1),1])*t.fees.maker.a, abs(order.fees[which(idx.1)+1,2])*t.fees.taker.b)
  transaction.fees[which(idx.2)+1,] <- cbind(abs(order.fees[which(idx.2),1])*t.fees.maker.a, abs(order.fees[which(idx.2)+1,2])*t.fees.taker.b)
  
  transaction.fees[which(idx.3)+1,] <- cbind(abs(order.fees[which(idx.3)+1,1])*t.fees.taker.a, abs(order.fees[which(idx.3),2])*t.fees.maker.b)
  transaction.fees[which(idx.4)+1,] <- cbind(abs(order.fees[which(idx.4)+1,1])*t.fees.taker.a, abs(order.fees[which(idx.4),2])*t.fees.maker.b)
  
  transaction.fees.cum              <- cbind(cumsum(transaction.fees[,1]), cumsum(transaction.fees[,2]))
  
  # legacy pnl implementation
  # ###########################################################################################################
  # ###########  PnL calculation -> seggregate between INVERSE SWAP and LINEAR PRODUCTS
  # ###########################################################################################################
  # position.contract.a <- 0
  # position.contract.b <- 0
  # 
  # position.usd.a      <- 0
  # position.usd.b      <- 0
  # 
  # # if fees == 0
  # #transaction.fees.cum  <-  transaction.fees.cum *0
  # 
  # t.fees.a <- 0
  # t.fees.b <- 0
  # if(type.a=="INVERSE") {
  #   #pnl.cum.a <- inverse.swap.pnl.fees(trade.price[idx.xing,1:3], clean.order.rounded[idx.xing,1], safe.position.a.b[idx.xing,1], t.fees.a) - cumsum(transaction.fees[idx.xing,1])
  #   pnl.cum.a               <- inverse.swap.pnl.fees(trade.price[idx.xing,1:3], clean.order.rounded[idx.xing,1], safe.position.a.b[idx.xing,1], t.fees.a) - transaction.fees.cum[idx.xing,1]
  # } else {
  #   linear.contr.pnl.a      <- linear.pnl.fees.v2(trade.price[idx.xing,1:3], clean.order.rounded[idx.xing,1], min.order.size.a, t.fees.a)
  #   position.contract.a     <- linear.contr.pnl.a$position.contract
  #   position.usd.a          <- linear.contr.pnl.a$position.usd
  #   #pnl.cum.a               <- linear.contr.pnl.a$usd.profit.cum - cumsum(transaction.fees[idx.xing,1])
  #   pnl.cum.a               <- linear.contr.pnl.a$usd.profit.cum - transaction.fees.cum[idx.xing,1]
  # }
  # 
  # if(type.b=="INVERSE") {
  #   #pnl.cum.b <- inverse.swap.pnl.fees(trade.price[idx.xing,c(1,4,5)], clean.order.rounded[idx.xing,2], safe.position.a.b[idx.xing,2], t.fees.b) - cumsum(transaction.fees[idx.xing,2])
  #   pnl.cum.b               <- inverse.swap.pnl.fees(trade.price[idx.xing,c(1,4,5)], clean.order.rounded[idx.xing,2], safe.position.a.b[idx.xing,2], t.fees.b) - transaction.fees.cum[idx.xing,2]
  # } else {
  #   linear.contr.pnl.b      <- linear.pnl.fees.v2(trade.price[idx.xing,c(1,4,5)], clean.order.rounded[idx.xing,2], min.order.size.b, t.fees.b)
  #   position.contract.b     <- linear.contr.pnl.b$position.contract
  #   position.usd.b          <- linear.contr.pnl.b$position.usd
  #   #pnl.cum.b               <- linear.contr.pnl.b$usd.profit.cum - cumsum(transaction.fees[idx.xing,2])
  #   pnl.cum.b               <- linear.contr.pnl.b$usd.profit.cum - transaction.fees.cum[idx.xing,2]
  # }
  # 
  # ###########################################################################################################
  # ###########  Build export object
  # ###########################################################################################################
  # # aggregate results
  # usd.profit.cum                                <- cbind(pnl.cum.a, pnl.cum.b)
  # pnl.wo.mh                                     <- list(pnl.wo.mh=pnl.cum.a + pnl.cum.b,  safe.position.a.b=safe.position.a.b, usd.profit.cum=usd.profit.cum)
  
  if(!is.daily.hedged) {
    ###########################################################################################################
    ###########  PnL calculation -> Version 2.0
    ###########  Inverse swap -> i.e. coin margined products
    ###########################################################################################################
    # core build source object
    # transaction.fees[idx.xing,]
    # trade.price[idx.xing,1:3]
    # safe.position.a.b[idx.xing,]
    
    t.fees.bc.a <- transaction.fees[idx.xing,1]/trade.price[idx.xing,3]
    t.fees.bc.b <- transaction.fees[idx.xing,2]/trade.price[idx.xing,5]
    
    # BEWARE SIGN OF CLEAN ORDER ROUNDED FOR PNL CALCULATION!!!
    pnl.dsc.a   <- cbind(-clean.order.rounded[idx.xing,1], clean.order.rounded[idx.xing,1]/trade.price[idx.xing,2]-t.fees.bc.a)
    pnl.dsc.b   <- cbind(-clean.order.rounded[idx.xing,2], clean.order.rounded[idx.xing,2]/trade.price[idx.xing,4]-t.fees.bc.b)
    
    pnl.cum.a <- cbind(cumsum(pnl.dsc.a[,1]),cumsum(pnl.dsc.a[,2]))
    pnl.cum.b <- cbind(cumsum(pnl.dsc.b[,1]),cumsum(pnl.dsc.b[,2]))
    
    # format to legacy format, WARNING: remove/comment 2 following lines if proceed to validation, see below...
    pnl.cum.a <- pnl.cum.a[,1]+pnl.cum.a[,2]*trade.price[idx.xing,3]
    pnl.cum.b <- pnl.cum.b[,1]+pnl.cum.b[,2]*trade.price[idx.xing,5]
  } else {

    ###########################################################################################################
    ###########  PnL calculation -> Version 3.0
    ###########  Inverse swap -> i.e. coin margined products
    ###########  hedge accumulation spot inventory daily
    ###########################################################################################################
    # date time conversion
    # options(digits=16) # print all digits
    # example:
    # tmp.date.time <- as.POSIXct(c("2024-07-24T20:34:26.999T000000Z", "2024-07-24T20:34:27.000T000000Z", "2024-07-24T20:34:27.007T000000Z"), format="%Y-%m-%dT%H:%M:%OST000000Z")
    # tmp.date.time
    # tmp.unix      <- as.numeric(tmp)
    # tmp.unix
    # as.POSIXct(tmp.unix, origin="1970-01-01")
    # end example
    
    # loop through days and compute intraday cummulative pnl
    previous.quote.position.a <- 0
    previous.quote.position.b <- 0
    
    liquidation.mid.price.a <- NULL
    liquidation.mid.price.b <- NULL
    
    # dbg
    trade.serie.a <- NULL
    trade.serie.b <- NULL
    
    fees.serie.a  <- NULL
    fees.serie.b  <- NULL
    
    # keep track of daily pnl for daily pnl sharpe calculation
    daily.date.serie  <- NULL
    
    # end dbg
    
    for(w in 1:length(idx.oos.eod)) {
      
      # find crossings within that day (if any)
      if(w==1){
        idx.day      <- 1:idx.oos.eod[1]
      } else {
        idx.day      <- (idx.oos.eod[w-1]+1):idx.oos.eod[w]
      }
      
      # daily crossing
      idx.xing.day <- idx.xing[idx.day]
      
      # compute liquidation price and entry price
      entry.mid.price.a <- ifelse(is.null(liquidation.mid.price.a), head(prices.bbo.a.b.oos[idx.day,2]+prices.bbo.a.b.oos[idx.day,3],1)/2, liquidation.mid.price.a)
      entry.mid.price.b <- ifelse(is.null(liquidation.mid.price.b), head(prices.bbo.a.b.oos[idx.day,4]+prices.bbo.a.b.oos[idx.day,5],1)/2, liquidation.mid.price.b)
      
      liquidation.mid.price.a <- tail(prices.bbo.a.b.oos[idx.day,2]+prices.bbo.a.b.oos[idx.day,3],1)/2
      liquidation.mid.price.b <- tail(prices.bbo.a.b.oos[idx.day,4]+prices.bbo.a.b.oos[idx.day,5],1)/2
      
      # # # compute liquidation price and entry price
      # liquidation.mid.price.a <- tail(prices.bbo.a.b[idx.day,2]+prices.bbo.a.b[idx.day,3],1)/2
      # liquidation.mid.price.b <- tail(prices.bbo.a.b[idx.day,4]+prices.bbo.a.b[idx.day,5],1)/2
      # 
      # entry.mid.price.a <- head(prices.bbo.a.b[idx.day,2]+prices.bbo.a.b[idx.day,3],1)/2
      # entry.mid.price.b <- head(prices.bbo.a.b[idx.day,4]+prices.bbo.a.b[idx.day,5],1)/2
      
      pnl.dsc.a   <- NULL
      pnl.dsc.b   <- NULL
      
      # check that at least one crossing occured during that day
      if(any(idx.xing.day)) {
        
        # compute intraday cummulative pnl for that day
        #print(w)
        
        # JF1
        # beware if there is only one crossing that day...not a matrix but a vector...
        if(length(idx.xing.day)==1) {
          t.fees.bc.a <- as.numeric(transaction.fees[idx.day,][1]/trade.price[idx.day,][3])
          t.fees.bc.b <- as.numeric(transaction.fees[idx.day,][2]/trade.price[idx.day,][5])
        } else {
          t.fees.bc.a <- transaction.fees[idx.day,][idx.xing.day,1]/trade.price[idx.day,][idx.xing.day,3]
          t.fees.bc.b <- transaction.fees[idx.day,][idx.xing.day,2]/trade.price[idx.day,][idx.xing.day,5]
        }
        
        # t.fees.bc.a <- transaction.fees[idx.day,][idx.xing.day,1]/trade.price[idx.day,][idx.xing.day,3]
        # t.fees.bc.b <- transaction.fees[idx.day,][idx.xing.day,2]/trade.price[idx.day,][idx.xing.day,5]
        
        # add fake entry to the trades
        # old
        # pnl.dsc.a   <- cbind(previous.quote.position.a, -previous.quote.position.a/entry.mid.price.a)
        # pnl.dsc.b   <- cbind(previous.quote.position.b, -previous.quote.position.b/entry.mid.price.b)
        # end old
        
        fees.serie.a  <- c(fees.serie.a, 0)
        fees.serie.b  <- c(fees.serie.b, 0)
        
        # dbg
        
        
        if(is.null(trade.serie.a)) {
          trade.serie.a <- c(previous.quote.position.a, entry.mid.price.a)
        } else {
          trade.serie.a <- rbind(trade.serie.a, c(previous.quote.position.a, entry.mid.price.a))
        }
        
        if(is.null(trade.serie.b)) {
          trade.serie.b <- c(previous.quote.position.b, entry.mid.price.b)
        } else {
          trade.serie.b <- rbind(trade.serie.b, c(previous.quote.position.b, entry.mid.price.b))
        }
        
        # end dbg
        
        # process trx discrete pnl
        # old
        # pnl.dsc.a   <- rbind(pnl.dsc.a, cbind(-clean.order.rounded[idx.day,][idx.xing.day,1], clean.order.rounded[idx.day,][idx.xing.day,1]/trade.price[idx.day,][idx.xing.day,2]-t.fees.bc.a))
        # pnl.dsc.b   <- rbind(pnl.dsc.b, cbind(-clean.order.rounded[idx.day,][idx.xing.day,2], clean.order.rounded[idx.day,][idx.xing.day,2]/trade.price[idx.day,][idx.xing.day,4]-t.fees.bc.b))
        # end old
        
        # dbg
        
        # JF1
        # beware if there is only one crossing that day...not a matrix but a vector...
        if(length(idx.xing.day)==1) {
          trade.serie.a <- rbind(trade.serie.a, as.numeric(cbind(-clean.order.rounded[idx.day,][1], trade.price[idx.day,][2])))
          trade.serie.b <- rbind(trade.serie.b, as.numeric(cbind(-clean.order.rounded[idx.day,][2], trade.price[idx.day,][4])))
        } else {
          trade.serie.a <- rbind(trade.serie.a, cbind(-clean.order.rounded[idx.day,][idx.xing.day,1], trade.price[idx.day,][idx.xing.day,2]))
          trade.serie.b <- rbind(trade.serie.b, cbind(-clean.order.rounded[idx.day,][idx.xing.day,2], trade.price[idx.day,][idx.xing.day,4]))
        }
        
        # trade.serie.a <- rbind(trade.serie.a, cbind(-clean.order.rounded[idx.day,][idx.xing.day,1], trade.price[idx.day,][idx.xing.day,2]))
        # trade.serie.b <- rbind(trade.serie.b, cbind(-clean.order.rounded[idx.day,][idx.xing.day,2], trade.price[idx.day,][idx.xing.day,4]))
        
        fees.serie.a  <- c(fees.serie.a, t.fees.bc.a)
        fees.serie.b  <- c(fees.serie.b, t.fees.bc.b)
        
        # end dbg
        
        # keep track of positions in quote w/o liquidation
        previous.quote.position.a <- sum(pnl.dsc.a[,1])
        previous.quote.position.b <- sum(pnl.dsc.b[,1])
        
        # old
        # add fake exit trades to the trades
        # pnl.dsc.a   <- rbind(pnl.dsc.a, cbind(-previous.quote.position.a, previous.quote.position.a/liquidation.mid.price.a))
        # pnl.dsc.b   <- rbind(pnl.dsc.b, cbind(-previous.quote.position.b, previous.quote.position.b/liquidation.mid.price.b))
        # end old
        
        # dbg
        fees.serie.a  <- c(fees.serie.a, 0)
        fees.serie.b  <- c(fees.serie.b, 0)
        
        trade.serie.a <- rbind(trade.serie.a, c(-previous.quote.position.a, liquidation.mid.price.a))
        trade.serie.b <- rbind(trade.serie.b, c(-previous.quote.position.b, liquidation.mid.price.b))
        # end dbg
        
        # old
        # pnl.cum.quote.base.day.a <- cbind(cumsum(pnl.dsc.a[,1]),cumsum(pnl.dsc.a[,2]))
        # pnl.cum.quote.base.day.b <- cbind(cumsum(pnl.dsc.b[,1]),cumsum(pnl.dsc.b[,2]))
        # 
        # pnl.cum.day.a <- pnl.cum.quote.base.day.a[,1]+pnl.cum.quote.base.day.a[,2]*c(entry.mid.price.a, trade.price[idx.day,][idx.xing.day,3], liquidation.mid.price.a)
        # pnl.cum.day.b <- pnl.cum.quote.base.day.b[,1]+pnl.cum.quote.base.day.b[,2]*c(entry.mid.price.b, trade.price[idx.day,][idx.xing.day,5], liquidation.mid.price.b)
        # end old
      } else {
        
        # old
        # add fake entry to the trades
        # pnl.dsc.a   <- cbind(previous.quote.position.a, -previous.quote.position.a/entry.mid.price.a)
        # pnl.dsc.b   <- cbind(previous.quote.position.b, -previous.quote.position.b/entry.mid.price.b)
        # end old
        
        # dbg
        fees.serie.a  <- c(fees.serie.a, 0)
        fees.serie.b  <- c(fees.serie.b, 0)
        
        if(is.null(trade.serie.a)) {
          trade.serie.a <- c(previous.quote.position.a, entry.mid.price.a)
        } else {
          trade.serie.a <- rbind(trade.serie.a, c(previous.quote.position.a, entry.mid.price.a))
        }
        
        if(is.null(trade.serie.b)) {
          trade.serie.b <- c(previous.quote.position.b, entry.mid.price.b)
        } else {
          trade.serie.b <- rbind(trade.serie.b, c(previous.quote.position.b, entry.mid.price.b))
        }
        
        # end dbg
        # rem don't modify the positions/track
        
        # old
        # add fake exit trades to the trades
        # pnl.dsc.a   <- rbind(pnl.dsc.a, cbind(-previous.quote.position.a, previous.quote.position.a/liquidation.mid.price.a))
        # pnl.dsc.b   <- rbind(pnl.dsc.b, cbind(-previous.quote.position.b, previous.quote.position.b/liquidation.mid.price.b))
        # end old
        
        # dbg
        
        trade.serie.a <- rbind(trade.serie.a, c(-previous.quote.position.a, liquidation.mid.price.a))
        trade.serie.b <- rbind(trade.serie.b, c(-previous.quote.position.b, liquidation.mid.price.b))
        
        fees.serie.a  <- c(fees.serie.a, 0)
        fees.serie.b  <- c(fees.serie.b, 0)
        
        # end dbg
        
        # old
        # pnl.cum.quote.base.day.a <- cbind(cumsum(pnl.dsc.a[,1]),cumsum(pnl.dsc.a[,2]))
        # pnl.cum.quote.base.day.b <- cbind(cumsum(pnl.dsc.b[,1]),cumsum(pnl.dsc.b[,2]))
        # 
        # pnl.cum.day.a <- pnl.cum.quote.base.day.a[,1]+pnl.cum.quote.base.day.a[,2]*c(entry.mid.price.a, trade.price[idx.day,][idx.xing.day,3], liquidation.mid.price.a)
        # pnl.cum.day.b <- pnl.cum.quote.base.day.b[,1]+pnl.cum.quote.base.day.b[,2]*c(entry.mid.price.b, trade.price[idx.day,][idx.xing.day,5], liquidation.mid.price.b)
        # end old
      }
      
      daily.date.serie  <- c(daily.date.serie, rep(daily.date[w], nrow(trade.serie.a)-length(daily.date.serie)))
      
      # old
      # concatenate daily intraday pnl
      # if(w==1){
      #   pnl.cum.a <- pnl.cum.day.a
      #   pnl.cum.b <- pnl.cum.day.b
      # } else {
      #   pnl.cum.a <- c(pnl.cum.a, pnl.cum.day.a+tail(pnl.cum.a,1))
      #   pnl.cum.b <- c(pnl.cum.b, pnl.cum.day.b+tail(pnl.cum.b,1))
      # }
      # end old
    }
    
    # dbg
    # pnl.cum.org.a <- pnl.cum.a
    # pnl.cum.org.b <- pnl.cum.b
    # 
    # head(-clean.order.rounded[clean.order.rounded[,1]!=0,],10)
    # head(trade.serie.a,10)
    
    # JF: TO BE UPDATED 20250801
    pnl.dsc.a   <- cbind(trade.serie.a[,1], -trade.serie.a[,1]/trade.serie.a[,2]-fees.serie.a)
    pnl.dsc.b   <- cbind(trade.serie.b[,1], -trade.serie.b[,1]/trade.serie.b[,2]-fees.serie.b)

    # pnl.dsc.a   <- cbind(trade.serie.a[,1], -trade.serie.a[,1]/trade.serie.a[,2])
    # pnl.dsc.b   <- cbind(trade.serie.b[,1], -trade.serie.b[,1]/trade.serie.b[,2])
    
    pnl.cum.a <- cbind(cumsum(pnl.dsc.a[,1]),cumsum(pnl.dsc.a[,2]))
    pnl.cum.b <- cbind(cumsum(pnl.dsc.b[,1]),cumsum(pnl.dsc.b[,2]))
    
    # format to legacy format, WARNING: remove/comment 2 following lines if proceed to validation, see below...
    pnl.cum.a <- pnl.cum.a[,1]+pnl.cum.a[,2]*trade.serie.a[,2]
    pnl.cum.b <- pnl.cum.b[,1]+pnl.cum.b[,2]*trade.serie.b[,2]

    # pnl.cum.dbg.a <- pnl.cum.a[,1]+pnl.cum.a[,2]*trade.serie.a[,2]
    # pnl.cum.dbg.b <- pnl.cum.b[,1]+pnl.cum.b[,2]*trade.serie.b[,2]
    # 
    # par(mfrow=c(2,1))
    # plot(pnl.cum.a+pnl.cum.b, type='l')
    # #plot(pnl.cum.org.a+pnl.cum.org.a, type='l')
    
    # end dbg
    
    }
    
    ###########################################################################################################
    ###########  End PnL calculation -> Version 3.0
    ###########################################################################################################
  
  ###########################################################################################################
  ###########  pnl validation -> import private_trade csv file as ptrade object
  ###########################################################################################################
  # idx.a <- ptrade[,"PRODUCT_ID"]=="prodA"
  # idx.b <- ptrade[,"PRODUCT_ID"]=="prodB"
  # 
  # fees.bc.a <- ptrade[idx.a,"LAST_EXECUTED_SIZE"]*min.order.size.a/ptrade[idx.a,"LAST_EXECUTED_PRICE"]*ifelse(ptrade[idx.a,"IS_MAKER"]==1,t.fees.maker.a,t.fees.taker.a)
  # fees.bc.b <- ptrade[idx.b,"LAST_EXECUTED_SIZE"]*min.order.size.b/ptrade[idx.b,"LAST_EXECUTED_PRICE"]*ifelse(ptrade[idx.b,"IS_MAKER"]==1,t.fees.maker.b,t.fees.taker.b)
  # 
  # fees.lc.a <- ptrade[idx.a,"LAST_EXECUTED_SIZE"]*min.order.size.a*ifelse(ptrade[idx.a,"IS_MAKER"]==1,t.fees.maker.a,t.fees.taker.a)
  # fees.lc.b <- ptrade[idx.b,"LAST_EXECUTED_SIZE"]*min.order.size.b*ifelse(ptrade[idx.b,"IS_MAKER"]==1,t.fees.maker.b,t.fees.taker.b)
  # 
  # cbind(fees.bc.a, fees.bc.b)
  # cbind(fees.lc.a, fees.lc.b)
  # 
  # 
  # pnl.cpp.dsc.a <- cbind(ptrade[idx.a,"LAST_EXECUTED_SIZE"]*10*ifelse(ptrade[idx.a,"SIDE"]=="BUY",-1,1),
  #                           ptrade[idx.a,"LAST_EXECUTED_SIZE"]*10/ptrade[idx.a,"LAST_EXECUTED_PRICE"]*ifelse(ptrade[idx.a,"SIDE"]!="BUY",-1,1) - ptrade[idx.a,"FEE_QUANTITY"])
  # pnl.cpp.dsc.b <- cbind(ptrade[idx.b,"LAST_EXECUTED_SIZE"]*10*ifelse(ptrade[idx.b,"SIDE"]=="BUY",-1,+1),
  #                           ptrade[idx.b,"LAST_EXECUTED_SIZE"]*10/ptrade[idx.b,"LAST_EXECUTED_PRICE"]*ifelse(ptrade[idx.b,"SIDE"]!="BUY",-1,1) - ptrade[idx.b,"FEE_QUANTITY"])
  # 
  # pnl.cpp.cum.a <- cbind(cumsum(pnl.cpp.dsc.a[,1]), cumsum(pnl.cpp.dsc.a[,2]))
  # pnl.cpp.cum.b <- cbind(cumsum(pnl.cpp.dsc.b[,1]), cumsum(pnl.cpp.dsc.b[,2]))
  # 
  # # validation r implementation
  # # pnl.dsc.a <- rbind(pnl.dsc.a, tail(pnl.cpp.dsc.a,1))
  # # pnl.dsc.b <- rbind(pnl.dsc.b, tail(pnl.cpp.dsc.b,1))
  # 
  # pnl.cum.a <- cbind(cumsum(pnl.dsc.a[,1]),cumsum(pnl.dsc.a[,2]))
  # pnl.cum.b <- cbind(cumsum(pnl.dsc.b[,1]),cumsum(pnl.dsc.b[,2]))
  # 
  # pnl.cpp.cum.a
  # pnl.cum.a
  # 
  # plot(pnl.cpp.cum.a[,2], type='l')
  # lines(pnl.cum.a[,2], col=2)
  # 
  # pnl.cpp.cum.b
  # pnl.cum.b
  # 
  # pnl.cpp.cum.a
  # pnl.cum.a[1:30,]
  # 
  # transaction.fees[idx.xing,]
  # trade.price[idx.xing,1:3]
  # safe.position.a.b[idx.xing,]
  # 
  # ptrade[idx.a,"LAST_EXECUTED_PRICE"][1:17]
  # trade.price[which(idx.xing)[1:17],1:3]
  # 
  # 
  # safe.position.a.b[which(idx.xing)[1:16],1]
  # ptrade[idx.a,"LAST_EXECUTED_SIZE"][1:16]*10
  # 
  # idx.1                   <- quote.level[-nrow(quote.level),"aSellLevelA"] <= quote.level[-1,"bid.a"] & idx.xing[-1]
  # 
  # which(c(idx.1,idx.2,idx.3,idx.4))
  ###########################################################################################################
  ###########  end pnl validation
  ###########################################################################################################

  ###########################################################################################################
  ###########  Build export object
  ###########################################################################################################
  # aggregate results
  usd.profit.cum                                <- cbind(pnl.cum.a, pnl.cum.b)
  pnl.wo.mh                                     <- list(pnl.wo.mh=pnl.cum.a + pnl.cum.b,  safe.position.a.b=safe.position.a.b, usd.profit.cum=usd.profit.cum, daily.date.serie=daily.date.serie)
  
  return(pnl.wo.mh)
}


