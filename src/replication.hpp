/*
 * replication.hpp
 *
 *  Created on: 2013-4-22
 *      Author: wqy
 */

#ifndef REPLICATION_HPP_
#define REPLICATION_HPP_
#include "channel/all_includes.hpp"
#include "ardb.hpp"
#include "util/thread/thread.hpp"
#include "util/thread/thread_mutex_lock.hpp"
#include "util/thread/lock_guard.hpp"
#include <stdio.h>

using namespace ardb::codec;

namespace ardb
{
	struct SlaveConn
	{
			Channel* conn;
			std::string server_key;
			uint64 synced_cmd_seq;
			uint32 state;
			uint8 type;
			SlaveConn(Channel* c = NULL);
			SlaveConn(Channel* c, const std::string& key, uint64 seq);
	};

	struct OpKey
	{
			DBID db;
			std::string key;
			OpKey(const DBID& id, const std::string& k);
			bool operator<(const OpKey& other) const;
			bool Empty()
			{
				return db.empty();
			}
	};

	struct CachedOp
	{
			uint8 type;
			CachedOp(uint8 t) :
					type(t)
			{
			}
			virtual ~CachedOp()
			{
			}
	};
	struct CachedWriteOp: public CachedOp
	{
			OpKey key;
			CachedWriteOp(uint8 t, OpKey& k);
	};
	struct CachedCmdOp: public CachedOp
	{
			RedisCommandFrame* cmd;
			CachedCmdOp(RedisCommandFrame* c);
			~CachedCmdOp();
	};

	class Ardb;
	class ArdbServer;
	class ArdbServerConfig;

	class OpLogs: public Thread
	{
		private:
			ArdbServerConfig& m_cfg;
			Ardb* m_db;
			uint64 m_min_seq;
			uint64 m_max_seq;
			FILE* m_op_log_file;
			Buffer m_op_log_buffer;
			uint32 m_current_oplog_record_size;
			time_t m_last_flush_time;

			std::string m_server_key;

			ThreadMutexLock m_lock;
			DBID m_current_db;
			typedef std::list<Runnable*> TaskList;
			typedef std::map<uint64, CachedOp*> CachedOpTable;
			typedef std::map<OpKey, uint64> OpKeyIndexTable;
			CachedOpTable m_mem_op_logs;
			OpKeyIndexTable m_mem_op_idx;
			TaskList m_tasks;

			void Run();
			void Routine();
			void PostTask(Runnable* r);
			void LoadCachedOpLog(Buffer & buf);
			void LoadCachedOpLog(const std::string& file);
			void Load();
			void RemoveExistOp(OpKey& key);
			void RemoveOldestOp();
			void CheckCurrentDB(const DBID& db);
			void ReOpenOpLog();
			void RollbackOpLogs();
			void FlushOpLog();
			void WriteCachedOp(uint64 seq, CachedOp* op);
			uint64 SaveCmdOp(RedisCommandFrame* cmd, bool writeOpLog = true);
			uint64 SaveWriteOp(OpKey& opkey, uint8 type,
					bool writeOpLog = true);
		public:
			OpLogs(ArdbServerConfig& cfg, Ardb* db);
			int LoadOpLog(uint64& seq, Buffer& cmd);
			void SaveSetOp(const DBID& db, const Slice& key,
					const Slice& value);
			void SaveDeleteOp(const DBID& db, const Slice& key);
			void SaveFlushOp(const DBID& db);
			bool VerifyClient(const std::string& serverKey, uint64 seq);
			const std::string& GetServerKey()
			{
				return m_server_key;
			}
	};

	class SlaveClient: public ChannelUpstreamHandler<RedisCommandFrame>,
			public ChannelUpstreamHandler<Buffer>,
			public Runnable
	{
		private:
			ArdbServer* m_serv;
			Channel* m_client;
			SocketHostAddress m_master_addr;
			uint32 m_chunk_len;
			int m_slave_state;
			bool m_cron_inited;
			bool m_ping_recved;
			RedisCommandDecoder m_decoder;
			NullRedisReplyEncoder m_encoder;

			uint8 m_server_type;
			std::string m_server_key;
			uint64 m_sync_seq;

			void MessageReceived(ChannelHandlerContext& ctx,
					MessageEvent<RedisCommandFrame>& e);
			void MessageReceived(ChannelHandlerContext& ctx,
					MessageEvent<Buffer>& e);
			void ChannelClosed(ChannelHandlerContext& ctx,
					ChannelStateEvent& e);
			void ChannelConnected(ChannelHandlerContext& ctx,
					ChannelStateEvent& e);
			void Timeout();
			void Run();
		public:
			SlaveClient(ArdbServer* serv) :
					m_serv(serv), m_client(NULL), m_chunk_len(0), m_slave_state(
							0), m_cron_inited(false), m_ping_recved(false), m_chunk_len(
							0), m_server_type(0), m_server_key("-"), m_sync_seq(0)
			{
			}
			int ConnectMaster(const std::string& host, uint32 port);
			void Close();
			void Stop();
	};

	class ReplicationService: public Thread,
			public SoftSignalHandler,
			public ChannelUpstreamHandler<Buffer>
	{
		private:
			ChannelService m_serv;
			ArdbServer* m_server;
			bool m_is_saving;
			uint32 m_last_save;

			void Run();
			typedef std::deque<SlaveConn> SyncClientQueue;

			typedef std::map<uint32, SlaveConn> SlaveConnTable;
			SyncClientQueue m_waiting_slaves;
			ThreadMutexLock m_slaves_lock;
			SlaveConnTable m_slaves;

			SoftSignalChannel* m_soft_signal;
			OpLogs m_oplogs;

			void Routine();
			void PingSlaves();
			void OnSoftSignal(uint32 soft_signo, uint32 appendinfo);
			void ChannelClosed(ChannelHandlerContext& ctx,
					ChannelStateEvent& e);
			void MessageReceived(ChannelHandlerContext& ctx,
					MessageEvent<Buffer>& e)
			{

			}
			void CheckSlaveQueue();
			void FullSync(Channel* client);
		public:
			ReplicationService(ArdbServer* serv);
			void ServSlaveClient(Channel* client);
			void ServARSlaveClient(Channel* client,
					const std::string& serverKey, uint64 seq);
			void RecordChangedKeyValue(const DBID& db, const Slice& key,
					const Slice& value);
			void RecordDeletedKey(const DBID& db, const Slice& key);
			void RecordFlushDB(const DBID& db);
			int Save();
			int BGSave();
			uint32 LastSave()
			{
				return m_last_save;
			}
	};
}

#endif /* REPLICATION_HPP_ */