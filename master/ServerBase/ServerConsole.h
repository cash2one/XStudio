#pragma once
#ifndef _SERVER_CONSOLE_H_
#define _SERVER_CONSOLE_H_

#include "ini.h"

//-----------------------------------//
// [1/13/2014 albert.xu]
// 初始化控制台模块
//-----------------------------------//
xgc_void InitializeConsole( XGC::common::IniFile &ini );

//-----------------------------------//
// [1/13/2014 albert.xu]
// 清理控制台模块
//-----------------------------------//
xgc_void FinializeConsole();

//-----------------------------------//
// [1/13/2014 albert.xu]
// 屏幕日志打印
//-----------------------------------//
xgc_void ConsoleLog( xgc_lpcstr pszLogText );

//-----------------------------------//
// [3/5/2014 albert.xu]
// 屏幕逻辑
//-----------------------------------//
xgc_int32 ConsoleUpdate();

#endif // _SERVER_CONSOLE_H_
