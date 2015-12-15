#include <process.h>
#include <time.h>
#include <stdio.h>
#include <io.h>
#include <fcntl.h>
#include <sys/stat.h>
#include "debuger.h"
#include "logger.h"
#include "datetime.h"

#include "xsystem.h"

namespace XGC
{
	static unsigned int __stdcall CheckThread( LPVOID lpParams );
	static xgc_char gTimeLogPath[_MAX_PATH] = { 0 };
	static xgc_time32 gTimeoutSeconds = 500;

	InvokeWatcherMgr::InvokeWatcherMgr()
		: mFinished( false )
		, mThread( (xgc_handle) INVALID_HANDLE_VALUE )
		, pInvokeWatcherHead( xgc_nullptr )
	{
	}

	InvokeWatcherMgr::~InvokeWatcherMgr()
	{
		Stop();
		InvokeWatcher* pWatcher = pInvokeWatcherHead;
		while( pWatcher )
		{
			pInvokeWatcherHead = pWatcher->Next();
			SAFE_DELETE( pWatcher );

			pWatcher = pInvokeWatcherHead;
		}
	}

	xgc_bool InvokeWatcherMgr::Start()
	{
		if( !mFinished )
		{
			// ��������߳�
			mThread = (xgc_handle) _beginthreadex( xgc_nullptr, 0, CheckThread, xgc_nullptr, 0, xgc_nullptr );
			mFinished = ( mThread == (xgc_handle) INVALID_HANDLE_VALUE );
		}
		return mFinished;
	}

	xgc_void InvokeWatcherMgr::Stop()
	{
		if( !mFinished )
		{
			mFinished = true;
			if( mThread != INVALID_HANDLE_VALUE )
			{
				WaitForSingleObject( (HANDLE) mThread, INFINITE );
				mThread = xgc_nullptr;
			}
		}
	}

	xgc_bool InvokeWatcherMgr::IsFinish()
	{
		return mFinished;
	}

	xgc_void InvokeWatcherMgr::InsertInvokeWatcher( InvokeWatcher* pWatcher )
	{
		autolock lock( mSection );
		pWatcher->mNext = pInvokeWatcherHead;
		pInvokeWatcherHead = pWatcher;
	}

	static InvokeWatcherMgr* gpInvokeWatcherMgr = XGC_NEW InvokeWatcherMgr();
	static xgc_void DeleteInvokeWatcherMgr( xgc_void )
	{
		SAFE_DELETE( gpInvokeWatcherMgr );
	}
	static int gpInvokeWatcherMgrExit = atexit( DeleteInvokeWatcherMgr );

	InvokeWatcherMgr& getInvokeWatcherMgr()
	{
		return *gpInvokeWatcherMgr;
	}

	static unsigned int __stdcall CheckThread( LPVOID lpParams )
	{
		XGC_UNREFERENCED_PARAMETER( lpParams );
// 		int fd = -1;
// 		_sopen_s( &fd, gTimeLogPath, O_TEXT | O_WRONLY | O_APPEND | _O_CREAT, SH_DENYWR, S_IWRITE );
// 		if( fd != -1 )
		{
			while( !getInvokeWatcherMgr().IsFinish() )
			{
				autolock lock( getInvokeWatcherMgr().mSection );
				InvokeWatcher* pWatcher = (InvokeWatcher*) getInvokeWatcherMgr().pInvokeWatcherHead;
				while( pWatcher )
				{
					pWatcher->FroceWrite( gTimeLogPath, 1 );
					pWatcher = pWatcher->Next();
				}
				lock.free_lock();
				Sleep( 1000 );
			}
/*			_close( fd );*/
		}

		return 0;
	}

	__declspec( thread ) InvokeWatcher *gpInvokeWatcher = xgc_nullptr;

	InvokeWatcher* getInvokeWatcher()
	{
		if( getInvokeWatcherMgr().IsFinish() )
		{
			return xgc_nullptr;
		}

		if( gpInvokeWatcher == xgc_nullptr )
		{
			gpInvokeWatcher = XGC_NEW InvokeWatcher();
			getInvokeWatcherMgr().InsertInvokeWatcher( gpInvokeWatcher );
		}

		return gpInvokeWatcher;
	}

	InvokeWatcher::InvokeWatcher( void )
		: mLastUpdate( datetime::current_time() )
		, mCallDeep( 0 )
		, mRef( 0 )
		, mIsDirty( false )
	{
	}

	InvokeWatcher::~InvokeWatcher( void )
	{
	}

	void InvokeWatcher::FunctionBegin( const char* function, int line )
	{
		if( true == mIsClose )
			return;

		mLastUpdate = datetime::current_time();
		if( mCallDeep < XGC_COUNTOF( mStack ) )
		{
			mStack[mCallDeep].lpFileName = function;
			mStack[mCallDeep].nTime = mLastUpdate.to_ftime();
			mStack[mCallDeep].nLine = line;
			mIsDirty = true;
		}
		++mCallDeep;
	}

	void InvokeWatcher::FunctionEnd( xgc_time32 nTimeOut )
	{
		if( true == mIsClose )
			return;

		if( mCallDeep > 0 )
		{
			mCallDeep --;
			if( mCallDeep < XGC_COUNTOF( mStack ) )
			{
				datetime pre = datetime::from_ftime( mStack[mCallDeep].nTime );
				datetime now = datetime::current_time();
				if( now > pre )
				{
					timespan spn = now - pre;
					if( spn.to_millsecnods() > nTimeOut )
					{
						mIsClose = true;
						LOGEXT( mStack[mCallDeep].lpFileName, mStack[mCallDeep].nLine, LOGLVL_SYS_WARNING, "����ִ�г�ʱ%I64u����", spn.to_millsecnods() );
						mIsClose = false;
					}
				}
			}
		}
	}

	void InvokeWatcher::FroceWrite( xgc_lpcstr file, xgc_uint32 uDelaySeconds )
	{
		datetime now = datetime::current_time();
		auto deep = mCallDeep;
		// ��ʱ��д��־
		if( deep && mIsDirty && timespan( now - mLastUpdate ).to_seconds() >= (xgc_int64) uDelaySeconds )
		{
			char str[128];
			now.to_string( str, sizeof( str ) );
			FILE * fp = xgc_nullptr;
			if( fopen_s( &fp, file, "a+" ) == 0 && fp )
			{
				fputs( "--------------------------------------------------------------\n", fp );
				fputs( "invoke watcher log\n", fp );
				fprintf( fp, "%s.%u timeout %llu millsecnods\n", str, now.to_systime().milliseconds, ( now - mLastUpdate ).to_millsecnods() );
				fputs( "-----------------------------CALL STACK-----------------------\n", fp );

				xgc_size length = 0;
				for( xgc_uint32 i = 0; i < deep; ++i )
				{
					fprintf( fp, "%I64u : %s(%u)\n",
						mStack[deep - i - 1].nTime,
						mStack[deep - i - 1].lpFileName,
						mStack[deep - i - 1].nLine );
				}

				fputs( "--------------------------------------------------------------\n\n", fp );
				fflush( fp );
				fclose( fp );
			}
			mIsDirty = false;
			mLastUpdate = now;
		}
	}

	InvokeWatcherWarp::InvokeWatcherWarp( InvokeWatcher* pWatcher, xgc_lpcstr pFile, xgc_uint32 nLine ) : mWatcher( pWatcher )
	{
		if( mWatcher )
		{
			mWatcher->AddRef();
			mWatcher->FunctionBegin( pFile, nLine );
		}
	}

	InvokeWatcherWarp::~InvokeWatcherWarp()
	{
		if( mWatcher )
		{
			mWatcher->Release();
			mWatcher->FunctionEnd( gTimeoutSeconds );
		}
	}

	///
	/// ���ó�ʱ��־
	/// [12/3/2014] create by albert.xu
	///
	COMMON_API xgc_void SetDebugerLog( xgc_lpcstr pathname )
	{
		strcpy_s( gTimeLogPath, pathname );
	}

	///
	/// ���ó�ʱ��־
	/// [12/3/2014] create by albert.xu
	///
	COMMON_API xgc_void SetDebugerTimeout( xgc_time32 millseconds )
	{
		gTimeoutSeconds = millseconds;
	}

	struct MemStatus
	{
		xgc_size	index;	// ����λ��

		xgc_time32	time;	// ʱ��
		xgc_uint64	pmem;	// �����ڴ�
		xgc_uint64	vmem;	// �����ڴ�
		xgc_char	name[32];	// ����

		xgc_size	parent;		// ������λ��
		xgc_size	lastchild;	// ���һ���Ӷ����λ��
	};

	struct MemStatusHead
	{
		xgc_size	alloced;	// �ѷ����ڴ�
		xgc_size	current;	// ��ǰָ��
		xgc_lpcstr	pname;		// ��������
		xgc_size	parent;		// ������λ��
		MemStatus	status[1];	// MemStatus����
	};

	__declspec( thread ) xgc_lpvoid gpMemStatus = xgc_nullptr;
	xgc_lpcstr MemMark( xgc_lpcstr name, xgc_lpcstr parent/* = xgc_nullptr*/, xgc_int32( *Report )( xgc_lpcstr fmt, ... )/* = xgc_nullptr*/ )
	{
		if( gpMemStatus == xgc_nullptr )
		{
			// δ�����ڴ�ʱ
			gpMemStatus = malloc( sizeof(MemStatusHead) +sizeof(MemStatus) * 100 );
			MemStatusHead* pHead = (MemStatusHead*) gpMemStatus;
			pHead->alloced = 100;
			pHead->current = 0;
			pHead->pname = xgc_nullptr;
			pHead->parent = 0;
		}

		MemStatusHead* pHead = (MemStatusHead*) gpMemStatus;
		if( pHead->current == pHead->alloced )
		{
			// �ڴ治��
			xgc_lpvoid mem = realloc( gpMemStatus, sizeof(MemStatusHead) +sizeof(MemStatus) * ( pHead->alloced + 20 ) );
			if( mem == xgc_nullptr )
				return name;

			gpMemStatus = mem;
			pHead = (MemStatusHead*) gpMemStatus;
			pHead->alloced += 20;
		}

		// ��ȡ��Ϣ
		MemStatus& mMem = pHead->status[pHead->current];
		mMem.index = pHead->current;

		GetMemoryStatus( &mMem.pmem, &mMem.vmem );
		mMem.time = (xgc_time32) clock();
		strcpy_s( mMem.name, name ? name: "" );

		// ��һ��,�ڴ��¼��ʼ
		if( pHead->current == 0 || name == xgc_nullptr )
		{
			mMem.parent		= -1;
			mMem.lastchild	= 0;
		}
		// �����˸�������
		else
		{
			// �ҵ�ǰһ��
			MemStatus& mParent = pHead->status[pHead->parent];
			MemStatus& mLast = pHead->status[mParent.lastchild];

			if( parent ? strcmp( parent, mParent.name ) == 0 : parent == xgc_nullptr )
			{
				// �����˸�������,����ǰһ���������ͬ,˵����ͬ����.
				mMem.parent		= pHead->parent;
				mMem.lastchild	= pHead->current; // ָ���Լ�

				// ���¸����������Ӷ�������
				mParent.lastchild = pHead->current;
			}
			else if( strcmp( parent, mLast.name ) == 0 )
			{
				// �����˸�������,����ǰһ����ͬ,˵����ǰһ�������
				pHead->pname = mLast.name;
				pHead->parent = mLast.index;

				mMem.parent = mLast.index;
				mMem.lastchild = pHead->current;	// ָ���Լ�

				// ���¸����������Ӷ�������
				mLast.lastchild = mMem.index;
			}
			else
			{
				// �����˸�������, �����, ���ҵ���ȷ�ĸ��ڵ�
				xgc_size pos = pHead->parent;

				// ����ƥ��ĸ�����
				while( pos < pHead->alloced )
				{
					MemStatus& mParent = pHead->status[pos];
					if( strcmp( parent, mParent.name ) == 0 )
					{
						pHead->pname			= mParent.name;
						pHead->parent       = mParent.index;

						mMem.parent         = pos;
						mMem.lastchild      = pHead->current;

						mParent.lastchild	= pHead->current;
						break;
					}
					pos = mParent.parent;
				}
			}
		}

		++pHead->current;
		return name;
	}

	static xgc_size _MemMarkReport( xgc_size root, xgc_string& prefix, xgc_int32( *Report )( xgc_lpcstr fmt, ... ) )
	{
		MemStatusHead* pMemHeader = (MemStatusHead*) gpMemStatus;

		xgc_size pos = root;
		MemStatus& mMemRoot = pMemHeader->status[pos++];
		while( pos <= mMemRoot.lastchild )
		{
			MemStatus& mMemCurrent = pMemHeader->status[pos];
			xgc_size compair = ( mMemCurrent.index == mMemCurrent.lastchild ? mMemCurrent.index : mMemCurrent.lastchild ) + 1;
			MemStatus& mMemCompair = pMemHeader->status[compair];
			if( mMemCurrent.index == mMemCompair.index )
			{
				Report( "%s#%-15s# PMEM = %6.2fM, VMEM = %6.2fM"
					, prefix.c_str()
					, mMemCurrent.name
					, mMemCurrent.pmem
					, mMemCurrent.vmem
					);
			}
			else
			{
				Report( "%s#%-20s# PASS %7.3f SECONDS, PMEM = %6.2fM, INC %6.2fM, VMEM = %6.2fM, INC %6.2fM"
					, prefix.c_str()
					, mMemCurrent.name
					, ( mMemCompair.time - mMemCurrent.time ) / 1000.0
					, mMemCompair.pmem / ( 1024 * 1024 * 1.0 )
					, ( (xgc_int64) mMemCompair.pmem - (xgc_int64) mMemCurrent.pmem ) / ( 1024 * 1024 * 1.0 )
					, mMemCompair.vmem / ( 1024 * 1024 * 1.0 )
					, ( (xgc_int64) mMemCompair.vmem - (xgc_int64) mMemCurrent.vmem ) / ( 1024 * 1024 * 1.0 )
					);
			}

			if( mMemCurrent.lastchild != mMemCurrent.index )
			{
				prefix.append( "  " );
				pos = _MemMarkReport( pos, prefix, Report );
				prefix.erase( prefix.length() - 2, 2 );
				continue;
			}
			else
			{
				pos += 1;
			}
		}

		return pos;
	}

	xgc_void MemMarkReport( xgc_lpcstr name, xgc_int32( *Report )( xgc_lpcstr fmt, ... ) )
	{
		MemMark( xgc_nullptr );
		MemStatusHead* pMemHeader = (MemStatusHead*) gpMemStatus;

		xgc_size pos = 0;
		while( name && pos < pMemHeader->current )
		{
			MemStatus& mMemCurrent = pMemHeader->status[pos];
			if( strcmp( mMemCurrent.name, name ) == 0 )
				break;

			++pos;
		}

		if( pos == pMemHeader->current )
			pos = 0;

		xgc_string prefix = "";
		_MemMarkReport( pos, prefix, Report );
	}

	xgc_void MemMarkClear()
	{
		free( gpMemStatus );
		gpMemStatus = xgc_nullptr;
	}
}