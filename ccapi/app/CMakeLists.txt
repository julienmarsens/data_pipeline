cmake_minimum_required(VERSION 3.14)
set(NAME app)
project(${NAME})
set(CMAKE_CXX_STANDARD 17)
if(NOT APPLE AND NOT MSVC)
  set(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} -pthread")
endif()
if (WIN32)
  set(CMAKE_BUILD_TYPE "Release")
endif()
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Debug")
endif()
message(STATUS "CMAKE_BUILD_TYPE: ${CMAKE_BUILD_TYPE}")
get_filename_component(CCAPI_PROJECT_DIR ../ ABSOLUTE)
message(STATUS "CCAPI_PROJECT_DIR: ${CCAPI_PROJECT_DIR}")

get_filename_component(CCAPI_DIR "${CMAKE_CURRENT_SOURCE_DIR}/.." ABSOLUTE)
message(STATUS "CCAPI_DIR: ${CCAPI_DIR}")

# Set external dependencies directory inside ccapi
set(EXTERNALS_DIR "${CCAPI_DIR}/externals" CACHE PATH "Directory for external dependencies")

# Ensure the directory exists
file(MAKE_DIRECTORY ${EXTERNALS_DIR})

if(NOT CCAPI_LEGACY_USE_WEBSOCKETPP)
  message(STATUS "use boost beast websocket")
  if(NOT BOOST_INCLUDE_DIR)
    # Check if Boost is already downloaded
    if(EXISTS "${EXTERNALS_DIR}/boost")
      message(STATUS "Using existing Boost installation at ${EXTERNALS_DIR}/boost")
      set(BOOST_INCLUDE_DIR "${EXTERNALS_DIR}/boost")
      add_library(boost INTERFACE)
    else()
      include(ExternalProject)
      ExternalProject_Add(boost
        URL    https://archives.boost.io/release/1.80.0/source/boost_1_80_0.tar.gz
        DOWNLOAD_DIR     "${EXTERNALS_DIR}/downloads"
        SOURCE_DIR       "${EXTERNALS_DIR}/boost"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        CONFIGURE_COMMAND ""
        BUILD_COMMAND     ""
        INSTALL_COMMAND   ""
        TEST_COMMAND      ""
      )
      set(BOOST_INCLUDE_DIR "${EXTERNALS_DIR}/boost")
    endif()
  else()
    add_library(boost INTERFACE)
  endif()
  
  if(NOT RAPIDJSON_INCLUDE_DIR)
    # Check if RapidJSON is already downloaded
    if(EXISTS "${EXTERNALS_DIR}/rapidjson")
      message(STATUS "Using existing RapidJSON installation at ${EXTERNALS_DIR}/rapidjson")
      set(RAPIDJSON_INCLUDE_DIR "${EXTERNALS_DIR}/rapidjson/include")
      add_library(rapidjson INTERFACE)
    else()
      include(ExternalProject)
      ExternalProject_Add(rapidjson
        URL    https://github.com/Tencent/rapidjson/archive/refs/tags/v1.1.0.tar.gz
        DOWNLOAD_DIR     "${EXTERNALS_DIR}/downloads"
        SOURCE_DIR       "${EXTERNALS_DIR}/rapidjson"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        CONFIGURE_COMMAND ""
        BUILD_COMMAND     ""
        INSTALL_COMMAND   ""
        TEST_COMMAND      ""
      )
      set(RAPIDJSON_INCLUDE_DIR "${EXTERNALS_DIR}/rapidjson/include")
    endif()
  else()
    add_library(rapidjson INTERFACE)
  endif()
  
  if(NOT HFFIX_INCLUDE_DIR)
    # Check if HFfix is already downloaded
    if(EXISTS "${EXTERNALS_DIR}/hffix")
      message(STATUS "Using existing HFfix installation at ${EXTERNALS_DIR}/hffix")
      set(HFFIX_INCLUDE_DIR "${EXTERNALS_DIR}/hffix/include")
      add_library(hffix INTERFACE)
    else()
      include(ExternalProject)
      ExternalProject_Add(hffix
        URL    https://github.com/jamesdbrock/hffix/archive/refs/tags/v1.3.0.tar.gz
        DOWNLOAD_DIR     "${EXTERNALS_DIR}/downloads"
        SOURCE_DIR       "${EXTERNALS_DIR}/hffix"
        DOWNLOAD_EXTRACT_TIMESTAMP TRUE
        CONFIGURE_COMMAND ""
        BUILD_COMMAND     ""
        INSTALL_COMMAND   ""
        TEST_COMMAND      ""
      )
      set(HFFIX_INCLUDE_DIR "${EXTERNALS_DIR}/hffix/include")
    endif()
  else()
    add_library(hffix INTERFACE)
  endif()
	include_directories(${CCAPI_PROJECT_DIR}/include ${BOOST_INCLUDE_DIR} ${RAPIDJSON_INCLUDE_DIR} ${HFFIX_INCLUDE_DIR} ${OPENSSL_INCLUDE_DIR})
else()
  message(STATUS "use websocketpp")
  add_compile_definitions(CCAPI_LEGACY_USE_WEBSOCKETPP)
  set(RAPIDJSON_INCLUDE_DIR ${CCAPI_PROJECT_DIR}/dependency/rapidjson/include)
  set(WEBSOCKETPP_INCLUDE_DIR ${CCAPI_PROJECT_DIR}/dependency/websocketpp)
  set(BOOST_INCLUDE_DIR ${CCAPI_PROJECT_DIR}/dependency/boost)
  set(HFFIX_INCLUDE_DIR ${CCAPI_PROJECT_DIR}/dependency/hffix/include)
  include_directories(${CCAPI_PROJECT_DIR}/include ${WEBSOCKETPP_INCLUDE_DIR} ${BOOST_INCLUDE_DIR} ${HFFIX_INCLUDE_DIR} ${RAPIDJSON_INCLUDE_DIR} ${OPENSSL_INCLUDE_DIR})
endif()

set(APP_INCLUDE_DIR ${CCAPI_PROJECT_DIR}/app/include)
message(STATUS "APP_INCLUDE_DIR: ${APP_INCLUDE_DIR}")
include_directories(${APP_INCLUDE_DIR})

find_package(OpenSSL REQUIRED)

find_package(ZLIB REQUIRED)
link_libraries(ZLIB::ZLIB)
set(ADDITIONAL_LINK_LIBRARIES "")
if (WIN32)
  set(ADDITIONAL_LINK_LIBRARIES ws2_32)
endif()
link_libraries(OpenSSL::Crypto OpenSSL::SSL ${ADDITIONAL_LINK_LIBRARIES})
add_compile_options(-Wno-deprecated -Wno-nonnull -Wno-deprecated-declarations)

#add_compile_definitions(CCAPI_ENABLE_EXCHANGE_COINBASE)
#
#add_compile_definitions(CCAPI_ENABLE_EXCHANGE_GEMINI)
#
#add_compile_definitions(CCAPI_ENABLE_EXCHANGE_KRAKEN)
#
#add_compile_definitions(CCAPI_ENABLE_EXCHANGE_BITSTAMP)
#
#add_compile_definitions(CCAPI_ENABLE_EXCHANGE_BITFINEX)
#
#add_compile_definitions(CCAPI_ENABLE_EXCHANGE_BINANCE_US)
#add_compile_definitions(CCAPI_ENABLE_EXCHANGE_BINANCE)
#
add_compile_definitions(CCAPI_ENABLE_EXCHANGE_OKX)
#
#add_compile_definitions(CCAPI_ENABLE_EXCHANGE_KUCOIN)
#
#add_compile_definitions(CCAPI_ENABLE_EXCHANGE_GATEIO)
#
#add_compile_definitions(CCAPI_ENABLE_EXCHANGE_CRYPTOCOM)
#
#add_compile_definitions(CCAPI_ENABLE_EXCHANGE_BYBIT)
#
#add_compile_definitions(CCAPI_ENABLE_EXCHANGE_ASCENDEX)
#
#add_compile_definitions(CCAPI_ENABLE_EXCHANGE_BITGET)
#
#add_compile_definitions(CCAPI_ENABLE_EXCHANGE_BITMART)
#
#add_compile_definitions(CCAPI_ENABLE_EXCHANGE_MEXC)
#
# If backtesting, you may want to comment out the following line to improve run-time speed.
add_compile_definitions(CCAPI_APP_ENABLE_LOG_INFO)

# If uncommented, the program will print out more information.
add_compile_definitions(CCAPI_APP_ENABLE_LOG_DEBUG)

# If you have custom logics and want to reuse part of class EventHandlerBase.
# add_compile_definitions(CCAPI_APP_USE_CUSTOM_EVENT_HANDLER)
# get_filename_component(CUSTOM_EVENT_HANDLER_DIRECTORY ${CUSTOM_EVENT_HANDLER_PATH} DIRECTORY)
# include_directories(${CUSTOM_EVENT_HANDLER_DIRECTORY})

# If you have custom logics and need to repeatedly rebuild for backtesting, you may want to comment out the following line to improve build-time speed. If so, make sure to
# provide additional environment variables BASE_ASSET_OVERRIDE, QUOTE_ASSET_OVERRIDE, ORDER_PRICE_INCREMENT_OVERRIDE, and ORDER_QUANTITY_INCREMENT_OVERRIDE at run time.
# add_compile_definitions(CCAPI_APP_IS_BACKTEST)

add_subdirectory(src/cross_arbitrage_pm)

#add_subdirectory(src/spot_market_making)
#add_subdirectory(src/single_order_execution)