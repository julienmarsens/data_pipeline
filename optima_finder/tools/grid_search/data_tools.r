# read formatted data for further r processing
read_r_bbo_cpp_repl <- function(exchange_names, product_names, dates_to_proceed, path_to_source) {
  
  # path_to_source            <- "/Volumes/IsabelleV/jf/data/clean_r"
  # exchange_names            <- c("deribit", "binance-coin-futures")
  # product_names             <- c("btc-perpetual", "ethusd-perp")
  # dates_to_proceed          <- c("2023-11-01")#, "2023-11-02", "2023-11-03", "2023-11-04", "2023-11-05", "2023-11-06", "2023-11-07", "2023-11-08",
  #                                #"2023-11-09", "2023-11-10", "2023-11-11", "2023-11-12", "2023-11-13", "2023-11-14")
  
  data_src_bbo_ab           <- NULL
  for(i in 1:length(dates_to_proceed)) {
    
    cat(paste("Processing date: [", dates_to_proceed[i], "]\n", sep=""))
    
    # deribit__binance-coin-futures__btc-perpetual__solusd-perp__2023-11-01
    file_name           <- paste(paste(exchange_names[1], exchange_names[2], sep="__"), "__", 
                                 paste(product_names[1], product_names[2], sep="__"), "__", dates_to_proceed[i], ".csv", sep="")
    
    data_src_bbo_ab_i   <- read.table(paste(path_to_source, "/", file_name, sep=""), header = TRUE, sep = ",", stringsAsFactor=FALSE)   
    
    if(i==1) {
      data_src_bbo_ab    <- data_src_bbo_ab_i 
    } else {
      data_src_bbo_ab    <- rbind(data_src_bbo_ab, data_src_bbo_ab_i)
    }
    
  }
  
  return(data_src_bbo_ab)
}
