#ifndef _DEBUG_H
#define _DEBUG_H

#ifdef __cplusplus
  extern "C" {
#endif


#define ERROR   1
#define WARN    2
#define INFO    3


// DEBUG_LOG_CFG for debugging
// 0 : no debug
// 1 : print error
// 2 : print warning
// 3 : print info
#if DBG_LOG_CFG

#include "SEGGER_RTT.h"

#define DBG_STRCAT(a, b)       a##b                ///< concat without expand
#define DBG_STRCAT3(a, b, c)   a##b##c             ///< concat without expand

#define DBG_XSTRCAT(a, b)      DBG_STRCAT(a, b)     ///< expand then concat
#define DBG_XSTRCAT3(a, b, c)  DBG_STRCAT3(a, b, c)     ///< expand then concat


#define dbg_printf(...)     SEGGER_RTT_printf(0, __VA_ARGS__)

#define DBG_INIT()          SEGGER_RTT_Init()

// Log with Level
#define DBG_LOG(n, ...)     DBG_XSTRCAT(DBG_LOG, n)(__VA_ARGS__)

// Log Level 1: Error
#define DBG_LOG1(...)       dbg_printf(__VA_ARGS__)

// Log Level 2: Warn
#if DBG_LOG_CFG >= 2
  #define DBG_LOG2(...)    DBG_LOG1(__VA_ARGS__)
#endif

// Log Level 3: Info
#if DBG_LOG_CFG >= 3
  #define DBG_LOG3(...)    DBG_LOG1(__VA_ARGS__)
#endif


#endif // DBG_LOG_CFG

#ifndef DBG_INIT
  #define DBG_INIT()
#endif

#ifndef DBG_LOG
  #define DBG_LOG(n, ...)
#endif

// TODO replace all DBG_LOGn with DBG_LOG(n)

#define DBG_LOG0(...)

#ifndef DBG_LOG1
  #define DBG_LOG1(...)
#endif

#ifndef DBG_LOG2
  #define DBG_LOG2(...)
#endif

#ifndef DBG_LOG3
  #define DBG_LOG3(...)
#endif



#ifdef __cplusplus
  }
#endif

#endif // _DEBUG_H