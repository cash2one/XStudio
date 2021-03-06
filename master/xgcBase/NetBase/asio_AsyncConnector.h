#pragma once
#ifndef _ASIO_ASYNC_CONNECTOR_H_
#define _ASIO_ASYNC_CONNECTOR_H_
#include "Netbase.h"
#include "asio_header.h"

using namespace asio;

namespace XGC
{
	namespace net
	{
		/*!
		 *
		 * \brief 同步连接
		 *
		 * \author albert.xu
		 * \date 十一月 2015
		 */
		xgc_bool Connect( io_service& service, xgc_lpcstr addr, xgc_int16 port, PacketProtocal *protocal, IMessageQueue* queue, xgc_uint16 timeout );

		/*!
		*
		* \brief 异步连接
		*
		* \author albert.xu
		* \date 十一月 2015
		*/
		xgc_bool ConnectAsync( io_service& service, xgc_lpcstr addr, xgc_int16 port, PacketProtocal *protocal, IMessageQueue* queue, xgc_uint16 timeout );
	};
};

#endif //_ASIO_ASYNC_CONNECTOR_H_