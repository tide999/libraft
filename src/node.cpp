#include "raft.hpp"
#include <algorithm>

#ifndef __10MB__ 
#define __10MB__ 10*1024*1024
#endif

#ifndef __10000__ 
#define __10000__ 10000
#endif

#ifndef __SNAPSHOT_EXT__
#define __SNAPSHOT_EXT__ ".snapshot"
#endif

namespace raft
{
	struct replicate_cond_t
	{
		typedef std::map<log_index_t,
			replicate_cond_t*> replicate_waiters_t;

		acl_pthread_cond_t *cond_;
		acl_pthread_mutex_t *mutex_;
		status_t result_;
		log_index_t log_index_;

		replicate_waiters_t::iterator it_;

		replicate_cond_t();
		~replicate_cond_t();
		void notify(status_t status = status_t::E_OK);
	};

	replicate_cond_t::replicate_cond_t()
		:result_(status_t::E_UNKNOWN),
		log_index_(0)
	{
		acl_pthread_mutexattr_t attr;

		cond_ = acl_thread_cond_create();
		mutex_ = static_cast<acl_pthread_mutex_t*>(
			acl_mymalloc(sizeof(acl_pthread_mutex_t)));
		acl_pthread_mutex_init(mutex_, &attr);
	}

	replicate_cond_t::~replicate_cond_t()
	{
		acl_pthread_cond_destroy(cond_);
		acl_pthread_mutex_destroy(mutex_);
		acl_myfree(mutex_);
	}

	void replicate_cond_t::notify(status_t status)
	{
		acl_pthread_mutex_lock(mutex_);
		result_ = status;
		acl_pthread_cond_signal(cond_);
		acl_pthread_mutex_unlock(mutex_);
	}

	node::node()
		: log_manager_(NULL),
		  election_timeout_(3000),
		  last_log_index_(0),
		  committed_index_(0),
		  applied_index_(0),
		  current_term_(0),
		  role_(E_FOLLOWER),
		  snapshot_callback_(NULL),
		  snapshot_info_(NULL),
		  snapshot_tmp_(NULL),
		  last_snapshot_index_(0),
		  last_snapshot_term_(0),
		  max_log_size_(4 * 1024 * 1024),
		  max_log_count_(5),
		  compacting_log_(false),
		  election_timer_(*this),
		  log_compaction_worker_(*this),
		  apply_callback_(NULL),
	      apply_log_(*this)
	{

	}

	bool node::is_leader()
	{
		acl::lock_guard lg(metadata_locker_);
		return role_ == E_LEADER;
	}

	void node::bind_snapshot_callback(snapshot_callback *callback)
	{
		snapshot_callback_ = callback;
	}
	void node::bind_apply_callback(apply_callback *callback)
	{
		apply_callback_ = callback;
	}
	void node::set_snapshot_path(const std::string &path)
	{
		acl::lock_guard lg(metadata_locker_);
		snapshot_path_ = path;

		if(snapshot_path_.size())
		{
			char ch = snapshot_path_.back();
			if(ch != '/'  && ch != '\\')
			{
				snapshot_path_.push_back('/');
			}
		}

	}

	void node::set_log_path(const std::string &path)
	{
		log_path_ = path;
	}

	void node::set_metadata_path(const std::string &path)
	{
		metadata_path_ = path;
	}

	void node::set_max_log_size(size_t size)
	{
		max_log_size_ = size;
	}

	void node::set_max_log_count(size_t size)
	{
		max_log_count_ = size;
	}
	void node::init()
	{
		std::string filepath;
		if (get_snapshot(filepath))
		{
			version ver;
			acl::ifstream file;
			if (!file.open_read(filepath.c_str()))
			{
				logger_error("open_read error.%s", filepath.c_str());
				return;
			}
			if (!read(file, ver))
			{
				logger_error("read version error");
				return;
			}
			acl::lock_guard lg(metadata_locker_);
			last_snapshot_index_ = ver.index_;
			last_snapshot_term_ = ver.term_;
		}
	}
	std::pair<status_t, version> node::replicate(const std::string &data)
	{
		status_t	result;
		term_t		term = 0;
		log_index_t index = 0;

		if (!is_leader())
			return { E_NO_LEADER,version()};
		
		if (!write_log(data, index, term))
		{
			logger_fatal("write_log error.%s", acl::last_serror());
			return{ E_WRITE_LOG_ERROR, version()};
		}

		//todo make a replicate_cond_t pool for this;
		replicate_cond_t cond;

		cond.log_index_ = index;

		add_replicate_cond(&cond);

		notify_peers_replicate_log();

		acl_pthread_mutex_lock(cond.mutex_);

		acl_assert(!acl_pthread_cond_wait(cond.cond_, cond.mutex_));

		result = cond.result_;
		acl_pthread_mutex_unlock(cond.mutex_);
		return{ result, version(index ,term ) };
	}

	std::string node::raft_id()const
	{
		return raft_id_;
	}

	bool node::is_candicate()
	{
		acl::lock_guard lg(metadata_locker_);
		return role_ == E_CANDIDATE;
	}

	raft::term_t node::current_term()
	{
		acl::lock_guard lg(metadata_locker_);
		return current_term_;
	}
	std::string node::leader_id()
	{
		acl::lock_guard lg(metadata_locker_);
		return leader_id_;
	}
	void node::set_leader_id(const std::string &leader_id)
	{
		acl::lock_guard lg(metadata_locker_);
		if (leader_id_ != leader_id)
			logger("find new leader.%s", leader_id.c_str());
		leader_id_ = leader_id;
	}
	void node::set_current_term(term_t term)
	{
		acl::lock_guard lg(metadata_locker_);
		if (current_term_ < term)
			/*new term. has a vote to election who is leader*/
			vote_for_.clear();
		current_term_ = term;
	}

	node::role_t node::role()
	{
		acl::lock_guard lg(metadata_locker_);
		return role_;
	}

	void node::set_vote_for(const std::string& vote_for)
	{
		acl::lock_guard lg(metadata_locker_);
		vote_for_ = vote_for;
	}

	std::string node::vote_for()
	{
		acl::lock_guard lg(metadata_locker_);
		return vote_for_;
	}

	void node::set_role(role_t _role)
	{
		acl::lock_guard lg(metadata_locker_);
		role_ = _role;
	}

	void node::update_applied_index(log_index_t index)
	{
		acl::lock_guard lg(metadata_locker_);
		if(index != applied_index_ + 1)
		{
			applied_index_ = index;
		}
			
	}
	raft::log_index_t node::last_log_index() const
	{
		return log_manager_->last_index();
	}

	void node::set_last_log_index(log_index_t index)
	{
		acl::lock_guard lg(metadata_locker_);
		last_log_index_ = index;
	}

	void node::add_replicate_cond(replicate_cond_t *cond)
	{
		acl::lock_guard lg(replicate_conds_locker_);

		acl_pthread_mutex_lock(cond->mutex_);

		cond->it_ = replicate_conds_.insert(
			std::make_pair(cond->log_index_, cond)).first;

		acl_pthread_mutex_unlock(cond->mutex_);
	}
	void node::remove_replicate_cond(replicate_cond_t *cond)
	{
		acl::lock_guard lg(replicate_conds_locker_);

		acl_pthread_mutex_lock(cond->mutex_);

		if (cond->it_ != replicate_conds_.end())
			replicate_conds_.erase(cond->it_);
		cond->it_ = replicate_conds_.end();

		acl_pthread_mutex_unlock(cond->mutex_);
	}
	bool node::build_replicate_log_request(
		replicate_log_entries_request &requst,
		log_index_t index,
		int entry_size) const
	{
		requst.set_leader_id(raft_id_);
		requst.set_leader_commit(committed_index_);

		if (!entry_size)
			entry_size = __10000__;

		//log empty 
		if (last_log_index() == 0)
		{
			requst.set_prev_log_index(0);
			requst.set_prev_log_term(0);
			return true;
		}
		else if (index <= last_log_index())
		{
			std::vector<log_entry> entries;
			//index -1 for prev_log_term, set_prev_log_index
			if (log_manager_->read(index - 1, __10MB__,
				entry_size, entries))
			{
				requst.set_prev_log_index(entries[0].index());
				requst.set_prev_log_term(entries[0].index());
				//first one is prev log
				for (size_t i = 1; i < entries.size(); i++)
				{
					//copy
					*requst.add_entries() = entries[i];
				}
				// read log ok
				return true;
			}
		}
		else
		{
			/*peer match leader now .and just make heartbeat req*/
			log_entry entry;
			/*index -1 for prev_log_term, prev_log_index */
			if (log_manager_->read(index - 1, entry))
			{
				requst.set_prev_log_index(entry.index());
				requst.set_prev_log_term(entry.index());

				// read log ok
				return true;
			}
		}
		//read log failed
		return false;
	}

	std::vector<log_index_t> node::get_peers_match_index()
	{
		std::vector<log_index_t> indexs;
		std::map<std::string, peer*>::iterator it;

		acl::lock_guard lg(peers_locker_);

		for (it = peers_.begin();
			it != peers_.end(); ++it)
		{
			indexs.push_back(it->second->match_index());
		}

		return indexs;
	}

	void node::replicate_log_callback()
	{
		/*
		 * If there exists an N such that N > commitIndex, a majority
		 * of matchIndex[i] �� N, and log[N].term == currentTerm:
		 * set commitIndex = N (��5.3, ��5.4).
		*/
		if (!is_leader())
		{
			logger("not leader.");
			return;
		}

		std::vector<log_index_t>
			mactch_indexs = get_peers_match_index();

		mactch_indexs.push_back(last_log_index());//myself 

		std::sort(mactch_indexs.begin(), mactch_indexs.end());

		log_index_t majority_index
			= mactch_indexs[mactch_indexs.size() / 2];

		if (majority_index > committed_index())
		{
			set_committed_index(majority_index);
			notify_replicate_conds(majority_index);
		}
	}

	void node::build_vote_request(vote_request &req)
	{
		req.set_candidate(raft_id_);
		req.set_last_log_index(last_log_index());
		req.set_last_log_term(current_term());
	}

	int node::peers_count()
	{
		acl::lock_guard lg(peers_locker_);
		return (int)peers_.size();
	}

	void node::clear_vote_response()
	{
		acl::lock_guard lg(vote_responses_locker_);
		vote_responses_.clear();
	}

	void node::vote_response_callback(
		const std::string &peer_id,
		const vote_response &response)
	{
		if (response.term() < current_term())
		{
			logger("handle vote_response, but term is old");
			return;
		}

		if (role() != role_t::E_CANDIDATE)
		{
			logger("handle vote_response, but not candidate");
			return;
		}

		int nodes = peers_count() + 1;//+1 for myself
		int votes = 1;//myself

		vote_responses_locker_.lock();

		vote_responses_[peer_id] = response;
		std::map<std::string, vote_response>
			::iterator it = vote_responses_.begin();

		for (; it != vote_responses_.end(); ++it)
		{
			if (it->second.vote_granted())
			{
				votes++;
			}
		}
		vote_responses_locker_.unlock();

		/*
		 * If votes received from majority of servers:
		 * become leader
		 */
		if (votes > nodes / 2)
		{
			become_leader();
		}
	}

	void node::become_leader()
	{

		cancel_election_timer();

		set_role(role_t::E_LEADER);

		clear_vote_response();
		/*
		 * Reinitialized after election
		 * nextIndex[] for each server, index of the next log entry
		 * to send to that server (initialized to leader last log index + 1)
		 * matchIndex[] for each server, index of highest log entry
		 * known to be replicated on server
		 * (initialized to 0, increases monotonically)
		 */
		update_peers_next_index(last_log_index() + 1);

		update_peers_match_index(0);

		notify_peers_replicate_log();
	}

	void node::handle_new_term(term_t term)
	{
		logger("receive new term.%d", term);
		set_current_term(term);
		step_down();
	}

	bool node::get_snapshot(std::string &path) const
	{
		std::map<log_index_t, std::string> snapshot_files_ = scan_snapshots();

		if (snapshot_files_.size())
		{
			path = snapshot_files_.rbegin()->second;
			return true;
		}
		return false;
	}

	std::map<log_index_t, std::string> node::scan_snapshots() const
	{

		acl::scan_dir scan;
		const char* filepath = NULL;
		std::map<log_index_t, std::string> snapshots;

		if (scan.open(snapshot_path_.c_str(), false) == false)
		{
			logger_error("scan open error %s\r\n",
				acl::last_serror());
			return{};
		}

		while ((filepath = scan.next_file(true)) != NULL)
		{
			if (acl_strrncasecmp(filepath, __SNAPSHOT_EXT__,
				strlen(__SNAPSHOT_EXT__)) == 0)
			{
				version ver;
				acl::ifstream file;

				if (!file.open_read(filepath))
				{
					logger_error("open file error.%s",
						acl::last_serror());
					continue;
				}
				if (!read(file, ver))
				{
					logger_error("read_version file.%s",
						filepath);
					file.close();
					continue;
				}
				file.close();
				snapshots[ver.index_] = filepath;
			}
		}
		return  snapshots;
	}

	bool node::should_compact_log()
	{
		if (log_manager_->log_count() <= max_log_count_ ||
			check_compacting_log())
		{
			return false;
		}
		return true;
	}

	bool node::check_compacting_log()
	{
		acl::lock_guard lg(compacting_log_locker_);
		return compacting_log_;
	}

	void node::async_compact_log()
	{
		acl::lock_guard lg(compacting_log_locker_);

		//check again.
		if (!compacting_log_)
		{
			compacting_log_ = true;
			log_compaction_worker_.do_compact_log();
		}
	}

	bool node::make_snapshot() const
	{
		std::string filepath;
		if (snapshot_callback_->make_snapshot(snapshot_path_, filepath))
		{
			std::string snapshot_file = filepath;
			size_t pos = filepath.find_last_of('.');
			if (pos != filepath.npos)
			{
				snapshot_file = filepath.substr(0, pos);
			}
			else
			{
				snapshot_file += __SNAPSHOT_EXT__;
			}
			if (rename(filepath.c_str(), snapshot_file.c_str()))
			{
				logger("make_snapshot done.");
				return true;
			}
			logger_error("rename failed,%s",acl::last_serror());
		}
		logger_error("make_snapshot error.path:", snapshot_path_.c_str());
		return false;
	}
	void node::do_compaction_log() const
	{
		bool do_make_snapshot = false;

	try_again:

		int	delete_counts = 0;
		log_index_t snapshot_index = 0;
		std::string snapshot_filepath;

		do
		{
			acl::ifstream	file;
			version			ver;

			if (!get_snapshot(snapshot_filepath))
			{	
				logger("not snapshot exist");
				break;
			}
			if (file.open_read(snapshot_filepath.c_str()))
			{
				snapshot_index = ver.index_;
			}
			else
			{
				logger_error("open snapshot file,error,%s", 
					snapshot_filepath.c_str());
				break;
			}
			if (!read(file, ver))
			{
				logger_error("read snapshot vesion error");
			}

		} while (false);
		
		std::map<log_index_t, log_index_t> log_infos = 
			log_manager_->logs_info();

		if(snapshot_index)
		{
			std::map<log_index_t, log_index_t>::iterator it = 
				log_infos.begin();

			for (;it != log_infos.end(); ++it)
			{
				if (it->second <= snapshot_index)
				{
					delete_counts += log_manager_->discard_log(it->second);

					//delete half of logs
					if (delete_counts >= log_infos.size() / 2)
					{
						break;
					}
				}
			}
		}
		

		if (!delete_counts && !do_make_snapshot)
		{
			do_make_snapshot = true;
			if (make_snapshot())
				goto try_again;
			else
				logger_error("make_snapshot error");
		}

	}

	void node::set_committed_index(log_index_t index)
	{
		acl::lock_guard lg(metadata_locker_);

		/*multi thread update committed_index.
			maybe other update bigger first.*/
		if (committed_index_ < index)
			committed_index_ = index;
	}

	void node::set_election_timer()
	{
		unsigned int timeout = election_timeout_;

		srand(static_cast<unsigned int>(time(NULL) % 0xffffffff));
		timeout += rand() % timeout;

		election_timer_.set_timer(timeout);
		logger("set_election_timer, "
			"%d milliseconds later", timeout);
	}

	void node::cancel_election_timer()
	{
		election_timer_.cancel_timer();
	}

	void node::election_timer_callback()
	{

		/*
		 * this node lost heartbeat from leader
		 * and it has not leader now.so this node should elect to be new leader
		 */
		set_leader_id("");

		/*Rule For Followers:
		* If election timeout elapses without receiving AppendEntries
		* RPC from current leader or granting vote to candidate:
		* convert to candidate		*/
		if (role() == role_t::E_FOLLOWER && vote_for().size())
		{
			set_election_timer();
			return;
		}


		/*
		* : conversion to candidate, start election:
		* : Increment currentTerm
		* : Vote for self
		* : Reset election timer
		* : Send RequestVote RPCs to all other servers
		* : If election timeout elapses: start new election
		*/

		clear_vote_response();
		set_role(role_t::E_CANDIDATE);
		set_current_term(current_term() + 1);
		set_vote_for(raft_id_);
		notify_peers_to_election();
		set_election_timer();
	}

	raft::log_index_t node::committed_index()
	{
		acl::lock_guard lg(metadata_locker_);
		return committed_index_;
	}

	raft::log_index_t node::start_log_index()const
	{
		return log_manager_->start_index();
	}

	void node::notify_peers_to_election()
	{
		acl::lock_guard lg(peers_locker_);

		std::map<std::string, peer *>::iterator it = peers_.begin();
		for (; it != peers_.end(); ++it)
		{
			it->second->notify_election();
		}
	}

	void  node::update_peers_next_index(log_index_t index)
	{
		acl::lock_guard lg(peers_locker_);

		std::map<std::string, peer *>::iterator it = peers_.begin();
		for (; it != peers_.end(); ++it)
		{
			it->second->set_next_index(index);
		}
	}

	void node::update_peers_match_index(log_index_t index)
	{
		acl::lock_guard lg(peers_locker_);

		std::map<std::string, peer *>::iterator it = peers_.begin();
		for (; it != peers_.end(); ++it)
		{
			it->second->set_match_index(index);
		}
	}

	void node::notify_peers_replicate_log()
	{
		acl::lock_guard lg(peers_locker_);
		std::map<std::string, peer *>::iterator it = peers_.begin();
		for (; it != peers_.end(); ++it)
		{
			it->second->notify_repliate();
		}
	}

	bool node::handle_vote_request(const vote_request &req,
		vote_response &resp)
	{
		resp.set_req_id(req.req_id());
		resp.set_term(current_term());
		resp.set_log_ok(false);

		/* Reply false if term < currentTerm (��5.1)*/
		if (req.term() < current_term())
		{
			resp.set_vote_granted(false);
			return true;
		}
		/*
		 * If votedFor is null or candidateId, and candidate's log is at
		 * least as up-to-date as receiver's log, grant vote (��5.2, ��5.4)
		 */
		if (req.last_log_index() > last_log_index())
		{
			resp.set_log_ok(true);
		}
		else if (req.last_log_index() == last_log_index())
		{
			if (req.last_log_term() == log_manager_->last_term())
			{
				resp.set_log_ok(true);
			}
		}

		if (req.term() > current_term())
		{
			/*step down to follower then discover new node with higher term*/
			step_down();
			set_current_term(req.term());
		}

		if (req.term() == current_term())
		{
			if (resp.log_ok() && vote_for().empty())
			{
				set_vote_for(req.candidate());
				resp.set_vote_granted(true);
			}
		}
		resp.set_term(current_term());
		return true;
	}

	void node::do_apply_log()
	{
		log_index_t index = committed_index();

		for (log_index_t i = committed_index() + 1; i < index; ++i)
		{
			log_entry entry;
			version ver;
			if (log_manager_->read(i, entry))
			{
				ver.index_ = entry.index();
				ver.term_ = entry.term();
				if (!apply_callback_->apply(entry.log_data(), ver))
				{
					logger_error("replicate_callback error");
					return;
				}
				update_applied_index(i);
				continue;
			}
			logger_error("read log error");
			return;
		}
	}

	bool node::handle_replicate_log_request(
		const replicate_log_entries_request &req,
		replicate_log_entries_response &resp)
	{
		resp.set_req_id(req.req_id());
		/*currentTerm, for leader to update itself*/
		resp.set_term(current_term());
		resp.set_last_log_index(last_log_index());

		/*Reply false if term < currentTerm (��5.1)*/
		if (req.term() < current_term())
		{
			resp.set_success(false);
			return true;
		}
			

		/*
		 *If RPC request or response contains term T > currentTerm:
		 *set currentTerm = T, convert to follower (��5.1)
		 */
		set_current_term(req.term());
		step_down();
		set_leader_id(req.leader_id());

		resp.set_term(current_term());

		/*Reply false if log doesn't contain an entry at prevLogIndex
		 *whose term matches prevLogTerm (��5.3)
		 */
		if (req.prev_log_index() > last_log_index())
		{
			resp.set_success(false);
			return true;
		}
		else if (req.prev_log_index() == last_log_index())
		{

			if (req.prev_log_index() == last_snapshot_index())
			{
				if (req.prev_log_term() != last_snapshot_term())
				{
					logger_fatal ("cluster error.....");
					return true;
				}
			}
			else
			{
				/*
				* reply false if log does not contain an entry at prevLogIndex 
				* whose term matches prevLogTerm (��5.3)
				*/
				if (req.prev_log_term() != log_manager_->last_term())
				{
					resp.set_last_log_index(req.prev_log_index() - 1);
				}
			}
		}
		else if (req.prev_log_index() >= log_manager_->start_index())
		{
			log_entry entry;

			if (log_manager_->read(req.prev_log_index(), entry))
			{
				/*
				* check log_entry sync.
				*/
				if (req.prev_log_term() != entry.term())
				{
					resp.set_last_log_index(req.prev_log_index() - 1);
					return true;
				}
			}
			else
			{
				logger_fatal("read log error.");
				return true;
			}
		}
		else
		{
			resp.set_last_log_index(last_snapshot_index());
			return true;
		}

		resp.set_success(true);

		bool sync_log = true;

		for (int i = 0; i < req.entries_size(); i++)
		{
			const log_entry &entry = req.entries(i);
			if (sync_log && entry.index() <= last_log_index())
			{
				log_entry tmp;
				if (log_manager_->read(entry.index(), tmp))
				{
					if (entry.term() == tmp.term())
						continue;
					/*
					 *  If an existing entry conflicts with a new one 
					 *  (same index but different terms), delete the 					 *  existing entry and all that follow it (��5.3)					 */
					log_manager_->truncate(entry.index());
					sync_log = false;
				}
			}
			/* Append any new entries not already in the log */
			if (!log_manager_->write(entry))
			{
				logger_error("write log error...");
				return true;
			}
		}
		/*
		 *  If leaderCommit > commitIndex, 
		 *  set commitIndex = min(leaderCommit, index of last new entry)
		 */
		if (req.leader_commit() > committed_index())
		{
			log_index_t index = req.leader_commit();
			if (index > last_log_index())
			{
				index = last_log_index();
			}
			set_committed_index(index);
			apply_log_.do_apply();
		}

		return true;
	}

	void node::close_snapshot()
	{
		acl_assert(snapshot_info_);
		acl_assert(snapshot_tmp_);

		delete snapshot_info_;
		snapshot_info_ = NULL;

		snapshot_tmp_->close();
		delete snapshot_tmp_;
		snapshot_tmp_ = NULL;
	}

	acl::fstream* node::get_snapshot_tmp(const snapshot_info &info)
	{
		if (!snapshot_info_)
		{
			/*
			 *Create new snapshot file if first chunk (offset is 0)
			 */
			acl::string filepath = snapshot_path_.c_str();
			filepath.format_append("llu%.snapshot_tmp",
				info.last_snapshot_index());
			snapshot_info_ = new snapshot_info(info);
			acl_assert(!snapshot_tmp_);
			snapshot_tmp_ = new acl::fstream();
			if (!snapshot_tmp_->open_trunc(filepath))
			{
				logger_error("open filename error,filepath:%s,%s",
					filepath.c_str(),
					acl::last_serror());
				return NULL;
			}
			return snapshot_tmp_;
		}
		if (info != *snapshot_info_)
		{
			const char *filepath = snapshot_tmp_->file_path();

			logger_error("snapshot_info not match current snapshot temp file."
				"remove old snapshot file. %s", filepath);

			close_snapshot();
			remove(filepath);
			return get_snapshot_tmp(info);
		}
		return snapshot_tmp_;
	}

	void node::step_down()
	{
		if (role() == role_t::E_LEADER)
		{
			notify_replicate_conds(status_t::E_NO_LEADER);
		}
		else if (role() == role_t::E_CANDIDATE)
		{
			clear_vote_response();
		}
		set_role(role_t::E_FOLLOWER);
		set_election_timer();
	}

	void node::load_snapshot_file()
	{
		version ver;

		acl_assert(snapshot_tmp_);

		std::string filepath = snapshot_tmp_->file_path();

		acl_assert(snapshot_tmp_->fseek(0, SEEK_SET) != -1);
		if (!read(*snapshot_tmp_, ver))
		{
			logger_error("read snapshot file error.path :%s",
				snapshot_tmp_->file_path());
			close_snapshot();
			remove(filepath.c_str());
			return;
		}
		close_snapshot();

		std::string temp;
		if (get_snapshot(temp))
		{
			acl::ifstream file;
			version temp_ver;

			acl_assert(file.open_read(temp.c_str()));
			acl_assert(read(file, temp_ver));
			file.close();
			if (ver.index_ < temp_ver.index_ ||
				ver.term_ < temp_ver.term_)
			{
				logger("snapshot_tmp is old.%s",
					filepath.c_str());
				return;
			}
		}

		std::string sfilepath = filepath;
		while (sfilepath.size() && sfilepath.back() != '.')
			sfilepath.pop_back();
		sfilepath.pop_back();
		sfilepath += __SNAPSHOT_EXT__;

		/*save snapshot file*/
		if (rename(filepath.c_str(), sfilepath.c_str()) != 0)
		{
			logger_error("rename error.oldFilePath:%s,newFilePath:%s,%s",
				filepath.c_str(), sfilepath.c_str(), acl::last_serror());
		}

		/*it must be*/
		if (last_log_index() < ver.index_)
		{
			/*discard the entire log*/
			int count = log_manager_->discard_log(ver.index_);
			log_manager_->set_last_index(ver.index_);
			log_manager_->set_last_term(ver.term_);
			logger("log_manager delete %d log files", count);
		}
		else
		{
			logger_fatal("error snapshot.something error happened");
			return;
		}

		set_last_snapshot_index(ver.index_);
		set_last_snapshot_term(ver.term_);

		acl_assert(snapshot_callback_);
		/*
		 *  Reset state machine using snapshot contents
		 *  (and load snapshot��s cluster configuration)
		 */
		if (!snapshot_callback_->load_snapshot(sfilepath))
		{
			logger_error("receive_snapshot_callback failed,filepath:%s ",
				filepath.c_str());
			return;
		}
		logger("load_snapshot_file ok ."
			"filepath:%s,"
			"last_log_index:%d,"
			"last_log_term:%d,",
			sfilepath.c_str(), ver.index_, ver.term_);
		/* discard any existing or partial snapshot with a smaller index*/
	}

	bool node::handle_install_snapshot_requst(
		const install_snapshot_request &req,
		install_snapshot_response &resp)
	{
		acl::fstream *file = NULL;

		resp.set_req_id(resp.req_id());

		if (req.term() < current_term())
		{
			resp.set_bytes_stored(0);
			resp.set_term(current_term());
			return true;
		}

		step_down();
		set_current_term(req.term());
		set_leader_id(req.leader_id());

		acl::lock_guard lg(snapshot_locker_);
		acl_assert(file = get_snapshot_tmp(req.snapshot_info()));
		if (file->fsize() != req.offset())
		{
			logger("offset error");
			resp.set_bytes_stored(file->fsize());
			return true;
		}
		/* Write data into snapshot file at given offset*/
		const std::string &data = req.data();
		if (file->write(data.c_str(), data.size()) != data.size())
			logger_fatal("file write error.%s", acl::last_serror());

		resp.set_bytes_stored(file->fsize());

		/*. Reply and wait for more data chunks if done is false*/
		if (req.done())
		{
			load_snapshot_file();
		}
		return true;
	}

	log_index_t node::gen_log_index()
	{
		acl::lock_guard lg(metadata_locker_);
		return ++last_log_index_;
	}
	log_index_t node::last_snapshot_index()
	{
		acl::lock_guard lg(metadata_locker_);
		return last_snapshot_index_;
	}
	void node::set_last_snapshot_index(log_index_t index)
	{
		acl::lock_guard lg(metadata_locker_);
		last_snapshot_index_ = index;
	}
	term_t node::last_snapshot_term()
	{
		acl::lock_guard lg(metadata_locker_);
		return last_snapshot_term_;
	}

	void node::set_last_snapshot_term(term_t term)
	{
		acl::lock_guard lg(metadata_locker_);
		last_snapshot_term_ = term;
	}

	void node::make_log_entry(const std::string &data, log_entry &entry)
	{
		term_t term = current_term();

		entry.set_index(gen_log_index());
		entry.set_term(term);
		entry.set_log_data(data);
		entry.set_type(log_entry_type::e_raft_log);
	}

	bool node::write_log(const std::string &data, 
		log_index_t &index, term_t &term)
	{

		log_entry entry;

		make_log_entry(data, entry);
		index = log_manager_->write(entry);
		term = entry.term();

		if (!index)
		{
			logger_error("log write error");
			return false;
		}

		if (should_compact_log())
			async_compact_log();

		return true;
	}

	void node::notify_replicate_conds(log_index_t index, status_t status)
	{

		std::map<log_index_t, replicate_cond_t*>::iterator it = 
			replicate_conds_.begin();

		acl::lock_guard lg(replicate_conds_locker_);

		for (; it != replicate_conds_.end();)
		{
			if (it->first > index)
			{
				break;
			}
			it->second->notify(status);
			it = replicate_conds_.erase(it);
		}
	}

	node::apply_log::apply_log(node& _node)
		: node_(_node), 
		 do_apply_(false)
	{
		mutex_ = new acl_pthread_mutex_t;
		acl_pthread_mutex_init(mutex_, NULL);
		cond_ = acl_pthread_cond_create();
	}

	node::apply_log::~apply_log()
	{
		acl_pthread_mutex_lock(mutex_);
		to_stop_ = true;
		acl_pthread_cond_signal(cond_);
		acl_pthread_mutex_unlock(mutex_);
		
		//wait thread;
		wait();

		//release obj
		acl_pthread_mutex_destroy(mutex_);
		delete mutex_;
		acl_pthread_cond_destroy(cond_);
	}

	void node::apply_log::do_apply()
	{
		acl_pthread_mutex_lock(mutex_);
		do_apply_ = true;
		acl_pthread_cond_signal(cond_);
		acl_pthread_mutex_unlock(mutex_);
	}

	bool node::apply_log::wait_to_apply()
	{
		bool result = false;
		
		acl_pthread_mutex_lock(mutex_);
		if(!do_apply_ && !to_stop_)
			acl_pthread_cond_wait(cond_, mutex_);
		result = do_apply_;
		do_apply_ = false;
		acl_pthread_mutex_unlock(mutex_);
		return result && !to_stop_;
	}

	void* node::apply_log::run()
	{
		while(wait_to_apply())
		{
			node_.do_apply_log();
		}
		return NULL;
	}

	node::log_compaction::log_compaction(node &_node)
		:node_(_node)
	{

	}

	void* node::log_compaction::run()
	{
		node_.do_compaction_log();
		return NULL;
	}

	void node::log_compaction::do_compact_log()
	{
		set_detachable(true);
		start();
	}
}
