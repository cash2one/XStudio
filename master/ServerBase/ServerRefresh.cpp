#include "stdafx.h"
#include "ServerBase.h"
#include "ServerRefresh.h"
#include "ServerDatabase.h"

using namespace XGC::DB;

/// @var 刷新系统使用的专有定时器
static timer_manager refresh_clock;

///
/// 刷新信息
/// [1/20/2015] create by albert.xu
///
struct RefreshInfo
{
	/// @var 前一次调用时间
	xgc_time32 prev_invoke_time;
	/// @var 后一次调用时间
	xgc_time32 next_invoke_time;
	/// @var 系统名称
	xgc_char sysname[64];
	/// @var 刷新类型
	xgc_char type[128];
	/// @var 定时调用
	xgc_char clock_invoke[128];
	/// @var 检查调用
	xgc_char check_invoke[128];
	/// @var 定时器句柄
	timer_t  handle;
};

/// @var 刷新信息
static xgc_unordered_map< xgc_string, xgc_vector< RefreshInfo* > > gRefreshInfos;

/// @var 刷新系统配置
static xgc_char szConfName[64] = { 0 };
static xgc_char szConfPath[_MAX_PATH] = { 0 };

xgc_bool InitServerRefresh( IniFile &ini )
{
	FUNCTION_BEGIN;
	if( ini.IsExistSection( "RefreshConf" ) )
	{
		xgc_lpcstr lpConfName = ini.GetItemValue( "RefreshConf", "ConfigName", xgc_nullptr );
		XGC_ASSERT_RETURN( lpConfName, false, "未配置RefreshConf.ConfigName" );
		xgc_lpcstr lpConfPath = ini.GetItemValue( "RefreshConf", "ConfigPath", xgc_nullptr );
		XGC_ASSERT_RETURN( lpConfName, false, "未配置RefreshConf.ConfigPath" );

		strcpy_s( szConfName, lpConfName );

		xgc_char szTransform[_MAX_PATH] = { 0 };
		ini.TransformValue( szTransform, lpConfPath );
		GetNormalPath( szConfPath, sizeof( szConfPath ), "%s", szTransform );

		if( !LoadServerRefresh() )
		{
			USR_ERROR( "refresh init failed." );
			return false;
		}
	}

	FUNCTION_END;
	return true;
}

///
/// 分析调用模式
/// 调用模式中以'@'开始的为属性名，分析到第一个非字符、数字、下划线的字符终止.
/// 使用 @@ 转义为一个 @
/// 属性名只能以 字母、数字、下划线组成，且首字符不能是数字。
/// [1/21/2015] create by albert.xu
///
static xgc_bool AnalyseInvokePattern( CMarkupSTL &xml, xgc_lpcstr lpPattern, xgc_lpstr lpBuffer, xgc_size nSize )
{
	XGC_ASSERT_RETURN( lpPattern && lpBuffer && nSize, false );
	xgc_size nWrite = 0;

	xgc_lpcstr pArg = xgc_nullptr;
	for( xgc_size n = 0; lpPattern[n] && nSize > nWrite; ++n )
	{
		if( xgc_nullptr == pArg && lpPattern[n] == '@' )
		{
			if( lpPattern[++n] != '@' )
				pArg = lpPattern + n;

			// 首字母必须是字母或下划线开头
			if( !isalpha( *pArg ) && *pArg != '_' )
				return false;
		}

		if( pArg )
		{
			// 找到第一个不是字符也不是数字也不是'_'的字符则认为参数名已结束
			if( !isalnum( lpPattern[n] ) && lpPattern[n] != '_' )
			{
				xgc_char name[64] = { 0 };
				strncpy_s( name, pArg, lpPattern + n - pArg );

				xgc_char attr[64] = { 0 };
				XGC_ASSERT_RETURN( GetXMLAttribString( xml, name, attr, sizeof( attr ) ), false, "属性[%s]不存在", name );

				int cpy = sprintf_s( lpBuffer + nWrite, nSize - nWrite, "%s", attr );
				XGC_ASSERT_RETURN( cpy > 0, false, "属性[%s]缓冲不足, nWrite = %u", name, nWrite );

				nWrite += cpy;

				*( lpBuffer + nWrite ) = lpPattern[n];
				++nWrite;

				pArg = xgc_nullptr;
			}
		}
		else
		{
			*( lpBuffer + nWrite ) = lpPattern[n];
			++nWrite;
		}
	}

	if( nWrite >= nSize )
		return false;

	*( lpBuffer + nWrite ) = 0;
	return true;
}

xgc_bool LoadServerRefresh()
{
	FUNCTION_BEGIN;
	FiniServerRefresh();

	CMarkupSTL xml;
	if( !xml.Load( szConfPath ) )
	{
		USR_ERROR( "读取文件%s, 错误 - %s", szConfPath, xml.GetError().c_str() );
		return false;
	}

	xgc_bool bRet = false;

	if( !xml.FindElem( "refresh" ) || !xml.IntoElem() )
		return false;

	while( xml.FindElem( "server" ) )
	{
		xgc_string strConfName = GetXMLAttrValue<xgc_string>( xml, "name" );
		if( strConfName != szConfName )
			continue;

		bRet = xml.IntoElem();
		XGC_ASSERT_BREAK( bRet, "%s", xml.GetError().c_str() );

		xgc_char szSysName[64] = { 0 };
		while( xml.FindElem( "system" ) )
		{
			bRet = GetXMLAttribString( xml, "name", szSysName, sizeof( szSysName ) );
			XGC_ASSERT_BREAK( bRet, "%s", xml.GetError().c_str() );

			// 读取调用模式，模式中以@开始的为属性参数替代符，替代符后一直到第一个非匹配字符的取值为属性名。
			xgc_char szClockInvokePattern[128] = { 0 };
			xgc_char szCheckInvokePattern[128] = { 0 };

			xgc_char szAttribName[64] = { 0 };

			bRet = GetXMLAttribString( xml, "clock_invoke", szClockInvokePattern, sizeof( szClockInvokePattern ) );
			XGC_ASSERT_BREAK( bRet, "%s", xml.GetError().c_str() );
			bRet = GetXMLAttribString( xml, "check_invoke", szCheckInvokePattern, sizeof( szCheckInvokePattern ) );
			XGC_ASSERT_BREAK( bRet, "%s", xml.GetError().c_str() );

			bRet = xml.IntoElem();
			XGC_ASSERT_BREAK( bRet, "%s", xml.GetError().c_str() );

			while( xml.FindElem( "item" ) )
			{
				// 填充刷新项结构
				RefreshInfo * pInfo = XGC_NEW RefreshInfo;
				memset( pInfo, 0, _msize( pInfo ) );
				memcpy_s( pInfo->sysname, sizeof(pInfo->sysname), szSysName, sizeof( szSysName ) );

				xgc_char buf[64] = { 0 };
				bRet = GetXMLAttribString( xml, "start", buf, sizeof( buf ) );
				if( false == bRet )
				{
					XGC_ASSERT_MESSAGE( bRet, "read attrib start error" );
					SAFE_DELETE( pInfo );
					break;
				}

				bRet = GetXMLAttribString( xml, "type", pInfo->type, sizeof( pInfo->type ) );
				if( false == bRet )
				{
					XGC_ASSERT_MESSAGE( bRet, "read attrib type error" );
					SAFE_DELETE( pInfo );
					break;
				}

				pInfo->prev_invoke_time = (xgc_time32) datetime::convert( buf, time( xgc_nullptr ) ).to_ctime();
				pInfo->prev_invoke_time = (xgc_time32) refresh_clock.adjust_clock_lower_bound( pInfo->prev_invoke_time, pInfo->type );
				pInfo->next_invoke_time	= (xgc_time32) refresh_clock.adjust_clock_upper_bound( pInfo->prev_invoke_time, pInfo->type );

				xgc_char szPrevInvokeTime[64] = { 0 };
				xgc_char szNextInvokeTime[64] = { 0 };

				datetime::from_ctime( pInfo->prev_invoke_time ).to_string( szPrevInvokeTime, sizeof( szPrevInvokeTime ) );
				datetime::from_ctime( pInfo->next_invoke_time ).to_string( szNextInvokeTime, sizeof( szNextInvokeTime ) );

				if( 0 == pInfo->next_invoke_time )
				{
					XGC_ASSERT_MESSAGE( bRet, "adjust_clock error %I64u, %s", pInfo->prev_invoke_time, pInfo->type );
					SAFE_DELETE( pInfo );
					break;
				}

				// 分析调用模式，重建调用参数
				bRet = AnalyseInvokePattern( xml, szClockInvokePattern, pInfo->clock_invoke, sizeof( pInfo->clock_invoke ) );
				if( false == bRet )
				{
					XGC_ASSERT_MESSAGE( bRet, "clock invoke pattern analyse error %s", szClockInvokePattern );
					SAFE_DELETE( pInfo );
					break;
				}

				bRet = AnalyseInvokePattern( xml, szCheckInvokePattern, pInfo->check_invoke, sizeof( pInfo->check_invoke ) );
				if( false == bRet )
				{
					XGC_ASSERT_MESSAGE( bRet, "clock invoke pattern analyse error %s", szCheckInvokePattern );
					SAFE_DELETE( pInfo );
					break;
				}

				DBG_INFO( "刷新类型为 %s 上一次调用时间为[%s]，下一次调用时间为[%s]", pInfo->type, szPrevInvokeTime, szNextInvokeTime );

				auto func = [pInfo]( timer_t )->xgc_void
				{
					OnServerRefresh( (xgc_lpcstr) pInfo->clock_invoke, pInfo->prev_invoke_time, xgc_nullptr );

					pInfo->prev_invoke_time = pInfo->next_invoke_time;
					pInfo->next_invoke_time = (xgc_time32) refresh_clock.adjust_clock_upper_bound( pInfo->prev_invoke_time, pInfo->type );
				};

				pInfo->handle = refresh_clock.insert_clock( func, pInfo->prev_invoke_time, pInfo->type );

				if( INVALID_TIMER_HANDLE == pInfo->handle )
				{
					XGC_ASSERT_MESSAGE( bRet, "insert clock error %I64u, %s", pInfo->prev_invoke_time, pInfo->type );
					SAFE_DELETE( pInfo );
					break;
				}

				gRefreshInfos[szSysName].push_back( pInfo );
			}
			// end of refresh element
			xml.OutOfElem();
			if( false == bRet ) break;
		}

		// end of server element
		xml.OutOfElem();
		if( false == bRet ) break;
	}

	xml.OutOfElem();

	return bRet;
	FUNCTION_END;
	return false;
}

xgc_void StepServerRefresh()
{
	FUNCTION_BEGIN;
	refresh_clock.clock_step();
	FUNCTION_END;
}

xgc_void FiniServerRefresh()
{
	FUNCTION_BEGIN;
	for each( auto &itr in gRefreshInfos )
	{
		for each( auto ptr in itr.second )
		{
			if( ptr->handle != INVALID_TIMER_HANDLE )
			{
				refresh_clock.remove_event( ptr->handle );
				ptr->handle = INVALID_TIMER_HANDLE;
				delete ptr;
			}
		}
	}

	gRefreshInfos.clear();
	FUNCTION_END;
}

xgc_void UpdateServerRefresh( xgc_lpcstr lpSystem, xgc_lpvoid lpContext )
{
	FUNCTION_BEGIN;
	for each( auto &it in gRefreshInfos )
	{
		if( stricmp( it.first.c_str(), lpSystem ) != 0 )
			continue;
		
		for each( auto ptr in it.second )
		{
			OnServerRefresh( ptr->check_invoke, ptr->prev_invoke_time, lpContext );
		}
	}
	FUNCTION_END;
}
