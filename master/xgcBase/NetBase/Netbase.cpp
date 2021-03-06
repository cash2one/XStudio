#include "Netbase.h"
#include "asio_Header.h"

namespace XGC
{
	namespace net
	{
		xgc_bool		(*InitNetwork)( int workthreads ) = 0;
		xgc_void		(*FiniNetwork)() = 0;
		xgc_uintptr     (*StartServer)( xgc_lpcstr address, xgc_uint16 port, PacketProtocal &protocal, xgc_uint16 ping_invent, xgc_uint16 timeout, xgc_uint16 acceptor_count, IMessageQueue** msg_queue_ptr );
		xgc_uintptr     (*StartServerEx)( xgc_lpcstr address, xgc_uint16 port, PacketProtocal &protocal, xgc_uint16 ping_invent, xgc_uint16 timeout, xgc_uint16 acceptor_count, create_handler_func call, xgc_uintptr param );
		xgc_void		(*CloseServer)( xgc_uintptr server_h );
		xgc_bool		(*ConnectServer)( xgc_lpcstr address, xgc_uint16 port, PacketProtocal &protocal, xgc_uint16 timeout, IMessageQueue** msg_queue );
		xgc_bool	    (*ConnectServerAsync)( xgc_lpcstr address, xgc_uint16 port, PacketProtocal &protocal, xgc_uint16 timeout, IMessageQueue** msg_queue );
		xgc_void		(*SendPacket)( network_t handle, const void* data, size_t size ) = 0;
		xgc_void		(*SendLastPacket)( network_t handle, const void* data, size_t size ) = 0;
		xgc_void		(*SendPackets)( network_t *handle, xgc_uint32 count, const void* data, size_t size ) = 0;
		xgc_void		(*SendToGroup)( group_t group, const void* data, size_t size, xgc_bool toself ) = 0;
		xgc_void		(*CloseLink)( network_t handle ) = 0;
		xgc_uintptr		(*ExecuteState)( xgc_uint32 operate_code, xgc_uintptr param ) = 0;// 返回 0 表示成功, < 0 表示发生错误 > 0 表示可以正常执行,但有其他问题.

		__declspec( thread ) xgc_void* packet_base = 0;

		#define HEADER_SIZE (4)
		static xgc_size	asio_header_space()
		{
			return 4;
		}

		static xgc_size	asio_packet_length( const xgc_byte* header )
		{
			// return packet length
			return *(xgc_uint16*)header;
		}

		// filter your interesting packet, return event code
		static xgc_uint32 asio_packet_filter( const xgc_byte* header )
		{
			if( header[2] == SYSTEM_MESSAGE_TYPE )
			{
				switch( header[3] )
				{
				case EVENT_PING:
					return FILTER_SYSTEM_PING;
					break;
				case EVENT_PONG:
					return FILTER_SYSTEM_PONG;
					break;
				}
			}
			return FILTER_PASS;
		}

		// return packet base address;
		static xgc_size	asio_packet_system( xgc_byte event, xgc_byte* header )
		{
			// event = EVENT_CODE { EVENT_ACCEPT, EVENT_CLOSE, EVENT_ERROR, EVENT_CONNECT, EVENT_PING, EVENT_PONG }
			//switch( event )
			//{
			//case EVENT_ACCEPT:
			//case EVENT_CLOSE:
			//case EVENT_CONNECT:
			//case EVENT_ERROR:
			//case EVENT_PING:
			//case EVENT_PONG:
			//	header[2] = SYSTEM_MESSAGE_TYPE;
			//	header[3] = event;
			//	break;
			//}

			header[2] = SYSTEM_MESSAGE_TYPE;
			header[3] = event;

			return 4;
		}

		static xgc_size asio_packet_finial( xgc_byte* header, xgc_size length )
		{
			*(xgc_uint16*)header = (xgc_uint16)length;
			return length;
		}

		//static xgc_bool	asio_packet_verifiy( xgc_byte* header, xgc_uint32 size, xgc_lpvoid context)
		//{
		//	return true;
		//}

		//static xgc_uint32 asio_packet_decrypt( xgc_byte* header, xgc_uint32 size, xgc_lpvoid context)
		//{
		//	return true;
		//}

		//static xgc_uint32 asio_packet_encrypt( xgc_byte* header, xgc_uint32 size, xgc_lpvoid context)
		//{
		//	return true;
		//}

		NETBASE_API PacketProtocal ProtocalDefault = 
		{
			asio_header_space,
			asio_packet_length,
			asio_packet_filter,
			asio_packet_system,
			asio_packet_finial,
			xgc_nullptr, //asio_packet_verifiy,
			xgc_nullptr, //asio_packet_decrypt,
			xgc_nullptr, //asio_packet_encrypt,
		};

		#define SETUP_API( PIX, FNAME )	XGC_VERIFY( FNAME = XGC::net::##PIX##FNAME )
		//--------------------------------------------------------//
		//	created:	3:12:2009   14:27
		//	filename: 	net
		//	author:		Albert.xu
		//
		//	purpose:	加载网络库
		//--------------------------------------------------------//
		xgc_bool CreateNetwork( eNetLibrary type )
		{
			if( type == asio )
			{
				SETUP_API( asio_, InitNetwork );
				SETUP_API( asio_, FiniNetwork );
				SETUP_API( asio_, StartServer );
				SETUP_API( asio_, StartServerEx );
				SETUP_API( asio_, CloseServer );
				SETUP_API( asio_, ConnectServer );
				SETUP_API( asio_, ConnectServerAsync );
				SETUP_API( asio_, SendPacket );
				SETUP_API( asio_, SendLastPacket );
				SETUP_API( asio_, SendPackets );
				SETUP_API( asio_, SendToGroup );
				SETUP_API( asio_, CloseLink );
				SETUP_API( asio_, ExecuteState );
			}
			return true;
		}

		//--------------------------------------------------------//
		//	created:	3:12:2009   14:27
		//	filename: 	net
		//	author:		Albert.xu
		//
		//	purpose:	释放网络库
		//--------------------------------------------------------//
		xgc_void DestroyNetwork()
		{
		};

		//////////////////////////////////////////////////////////////////////////
		static pfnProcessor	dispatch[255][255];

		xgc_bool RegistFunction( xgc_byte type, xgc_byte id, pfnProcessor fn )
		{
			dispatch[type][id] = fn;
			return true;
		}

		xgc_bool UnRegistFunction( xgc_byte type, xgc_byte id )
		{
			dispatch[type][id] = 0;
			return true;
		}

		int DispatchMessage( IMessageQueue* msg_queue )
		{
			int nProcessPacketCount = 0;
			INetPacket* pPacketMsg = xgc_nullptr;

			while( msg_queue->PopMessage( &pPacketMsg ) )
			{
				const xgc_byte* header = (const xgc_byte*)pPacketMsg->header();
				pfnProcessor functor = dispatch[header[2]][header[3]];
				if( functor )
				{
					functor( pPacketMsg->handle(), pPacketMsg->userdata(), pPacketMsg->data(), pPacketMsg->size() - ( pPacketMsg->data() - pPacketMsg->header() ) );
				}

				pPacketMsg->release();
				nProcessPacketCount++;

				break;
			}

			return nProcessPacketCount;
		}

	}
}
