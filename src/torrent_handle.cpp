/*

Copyright (c) 2003-2016, Arvid Norberg
All rights reserved.

Redistribution and use in source and binary forms, with or without
modification, are permitted provided that the following conditions
are met:

    * Redistributions of source code must retain the above copyright
      notice, this list of conditions and the following disclaimer.
    * Redistributions in binary form must reproduce the above copyright
      notice, this list of conditions and the following disclaimer in
      the documentation and/or other materials provided with the distribution.
    * Neither the name of the author nor the names of its
      contributors may be used to endorse or promote products derived
      from this software without specific prior written permission.

THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT OWNER OR CONTRIBUTORS BE
LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE
POSSIBILITY OF SUCH DAMAGE.

*/

#include <ctime>
#include <iterator>
#include <algorithm>
#include <set>
#include <cctype>

#include "libtorrent/peer_id.hpp"
#include "libtorrent/bt_peer_connection.hpp"
#include "libtorrent/torrent_info.hpp"
#include "libtorrent/tracker_manager.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/entry.hpp"
#include "libtorrent/session.hpp"
#include "libtorrent/aux_/session_impl.hpp"
#include "libtorrent/aux_/session_call.hpp"
#include "libtorrent/invariant_check.hpp"
#include "libtorrent/utf8.hpp"
#include "libtorrent/announce_entry.hpp"

#if TORRENT_COMPLETE_TYPES_REQUIRED
#include "libtorrent/peer_info.hpp" // for peer_list_entry
#endif

using libtorrent::aux::session_impl;

namespace libtorrent
{

#ifndef BOOST_NO_EXCEPTIONS
	void throw_invalid_handle()
	{
		throw system_error(errors::invalid_torrent_handle);
	}
#endif

	template<typename Fun, typename... Args>
	void torrent_handle::async_call(Fun f, Args&&... a) const
	{
		std::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT_PRECOND(t);
		if (!t) return;
		session_impl& ses = static_cast<session_impl&>(t->session());
		ses.get_io_service().dispatch([=] () { (t.get()->*f)(a...); } );
	}

	template<typename Fun, typename... Args>
	void torrent_handle::sync_call(Fun f, Args&&... a) const
	{
		std::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT_PRECOND(t);
		if (!t) return;
		session_impl& ses = static_cast<session_impl&>(t->session());

		// this is the flag to indicate the call has completed
		bool done = false;

		ses.get_io_service().dispatch([=,&done,&ses] ()
		{
			(t.get()->*f)(a...);
			std::unique_lock<std::mutex> l(ses.mut);
			done = true;
			ses.cond.notify_all();
		} );

		aux::torrent_wait(done, ses);
	}

	template<typename Ret, typename Fun, typename... Args>
	Ret torrent_handle::sync_call_ret(Ret def, Fun f, Args&&... a) const
	{
		std::shared_ptr<torrent> t = m_torrent.lock();
		TORRENT_ASSERT_PRECOND(t);
		Ret r = def;
		if (!t) return r;
		session_impl& ses = static_cast<session_impl&>(t->session());

		// this is the flag to indicate the call has completed
		bool done = false;

		ses.get_io_service().dispatch([=,&r,&done,&ses] ()
		{
			r = (t.get()->*f)(a...);
			std::unique_lock<std::mutex> l(ses.mut);
			done = true;
			ses.cond.notify_all();
		} );

		aux::torrent_wait(done, ses);

		return r;
	}

	sha1_hash torrent_handle::info_hash() const
	{
		std::shared_ptr<torrent> t = m_torrent.lock();
		static const sha1_hash empty;
		if (!t) return empty;
		return t->info_hash();
	}

	int torrent_handle::max_uploads() const
	{
		return sync_call_ret<int>(0, &torrent::max_uploads);
	}

	void torrent_handle::set_max_uploads(int max_uploads) const
	{
		TORRENT_ASSERT_PRECOND(max_uploads >= 2 || max_uploads == -1);
		async_call(&torrent::set_max_uploads, max_uploads, true);
	}

	int torrent_handle::max_connections() const
	{
		return sync_call_ret<int>(0, &torrent::max_connections);
	}

	void torrent_handle::set_max_connections(int max_connections) const
	{
		TORRENT_ASSERT_PRECOND(max_connections >= 2 || max_connections == -1);
		async_call(&torrent::set_max_connections, max_connections, true);
	}

	void torrent_handle::set_upload_limit(int limit) const
	{
		TORRENT_ASSERT_PRECOND(limit >= -1);
		async_call(&torrent::set_upload_limit, limit);
	}

	int torrent_handle::upload_limit() const
	{
		return sync_call_ret<int>(0, &torrent::upload_limit);
	}

	void torrent_handle::set_download_limit(int limit) const
	{
		TORRENT_ASSERT_PRECOND(limit >= -1);
		async_call(&torrent::set_download_limit, limit);
	}

	int torrent_handle::download_limit() const
	{
		return sync_call_ret<int>(0, &torrent::download_limit);
	}

	void torrent_handle::move_storage(
		std::string const& save_path, int flags) const
	{
		async_call(&torrent::move_storage, save_path, flags);
	}

#if TORRENT_USE_WSTRING
#ifndef TORRENT_NO_DEPRECATE
	void torrent_handle::move_storage(
		std::wstring const& save_path, int flags) const
	{
		async_call(&torrent::move_storage, wchar_utf8(save_path), flags);
	}

	void torrent_handle::rename_file(int index, std::wstring const& new_name) const
	{
		async_call(&torrent::rename_file, index, wchar_utf8(new_name));
	}
#endif // TORRENT_NO_DEPRECATE
#endif // TORRENT_USE_WSTRING

	void torrent_handle::rename_file(int index, std::string const& new_name) const
	{
		async_call(&torrent::rename_file, index, new_name);
	}

	void torrent_handle::add_extension(
		std::function<std::shared_ptr<torrent_plugin>(torrent_handle const&, void*)> const& ext
		, void* userdata)
	{
#ifndef TORRENT_DISABLE_EXTENSIONS
		async_call(&torrent::add_extension_fun, ext, userdata);
#else
		TORRENT_UNUSED(ext);
		TORRENT_UNUSED(userdata);
#endif
	}

	bool torrent_handle::set_metadata(span<char const> metadata) const
	{
		return sync_call_ret<bool>(false, &torrent::set_metadata, metadata);
	}

	void torrent_handle::pause(int flags) const
	{
		async_call(&torrent::pause, bool(flags & graceful_pause));
	}

	void torrent_handle::stop_when_ready(bool b) const
	{
		async_call(&torrent::stop_when_ready, b);
	}

	void torrent_handle::apply_ip_filter(bool b) const
	{
		async_call(&torrent::set_apply_ip_filter, b);
	}

	void torrent_handle::set_share_mode(bool b) const
	{
		async_call(&torrent::set_share_mode, b);
	}

	void torrent_handle::set_upload_mode(bool b) const
	{
		async_call(&torrent::set_upload_mode, b);
	}

	void torrent_handle::flush_cache() const
	{
		async_call(&torrent::flush_cache);
	}

	void torrent_handle::set_ssl_certificate(
		std::string const& certificate
		, std::string const& private_key
		, std::string const& dh_params
		, std::string const& passphrase)
	{
#ifdef TORRENT_USE_OPENSSL
		async_call(&torrent::set_ssl_cert, certificate, private_key, dh_params, passphrase);
#else
		TORRENT_UNUSED(certificate);
		TORRENT_UNUSED(private_key);
		TORRENT_UNUSED(dh_params);
		TORRENT_UNUSED(passphrase);
#endif
	}

	void torrent_handle::set_ssl_certificate_buffer(
		std::string const& certificate
		, std::string const& private_key
		, std::string const& dh_params)
	{
#ifdef TORRENT_USE_OPENSSL
		async_call(&torrent::set_ssl_cert_buffer, certificate, private_key, dh_params);
#else
		TORRENT_UNUSED(certificate);
		TORRENT_UNUSED(private_key);
		TORRENT_UNUSED(dh_params);
#endif
	}

	void torrent_handle::save_resume_data(int f) const
	{
		async_call(&torrent::save_resume_data, f);
	}

	bool torrent_handle::need_save_resume_data() const
	{
		return sync_call_ret<bool>(false, &torrent::need_save_resume_data);
	}

	void torrent_handle::force_recheck() const
	{
		async_call(&torrent::force_recheck);
	}

	void torrent_handle::resume() const
	{
		async_call(&torrent::resume);
	}

	void torrent_handle::auto_managed(bool m) const
	{
		async_call(&torrent::auto_managed, m);
	}

	int torrent_handle::queue_position() const
	{
		return sync_call_ret<int>(-1, &torrent::queue_position);
	}

	void torrent_handle::queue_position_up() const
	{
		async_call(&torrent::queue_up);
	}

	void torrent_handle::queue_position_down() const
	{
		async_call(&torrent::queue_down);
	}

	void torrent_handle::queue_position_top() const
	{
		async_call(&torrent::set_queue_position, 0);
	}

	void torrent_handle::queue_position_bottom() const
	{
		async_call(&torrent::set_queue_position, INT_MAX);
	}

	void torrent_handle::clear_error() const
	{
		async_call(&torrent::clear_error);
	}

#ifndef TORRENT_NO_DEPRECATE
	void torrent_handle::set_priority(int) const {}

	void torrent_handle::set_tracker_login(std::string const& name
		, std::string const& password) const
	{
		async_call(&torrent::set_tracker_login, name, password);
	}
#endif

	void torrent_handle::file_progress(std::vector<std::int64_t>& progress, int flags) const
	{
		sync_call(&torrent::file_progress, std::ref(progress), flags);
	}

	torrent_status torrent_handle::status(std::uint32_t flags) const
	{
		torrent_status st;
		sync_call(&torrent::status, &st, flags);
		return st;
	}

	void torrent_handle::set_pinned(bool p) const
	{
		async_call(&torrent::set_pinned, p);
	}

	void torrent_handle::set_sequential_download(bool sd) const
	{
		async_call(&torrent::set_sequential_download, sd);
	}

	void torrent_handle::piece_availability(std::vector<int>& avail) const
	{
		auto availr = std::ref(avail);
		sync_call(&torrent::piece_availability, availr);
	}

	void torrent_handle::piece_priority(int index, int priority) const
	{
		async_call(&torrent::set_piece_priority, index, priority);
	}

	int torrent_handle::piece_priority(int index) const
	{
		return sync_call_ret<int>(0, &torrent::piece_priority, index);
	}

	void torrent_handle::prioritize_pieces(std::vector<int> const& pieces) const
	{
		async_call(&torrent::prioritize_pieces, pieces);
	}

	void torrent_handle::prioritize_pieces(std::vector<std::pair<int, int> > const& pieces) const
	{
		async_call(&torrent::prioritize_piece_list, pieces);
	}

	std::vector<int> torrent_handle::piece_priorities() const
	{
		std::vector<int> ret;
		auto retp = &ret;
		sync_call(&torrent::piece_priorities, retp);
		return ret;
	}

	void torrent_handle::file_priority(int index, int priority) const
	{
		async_call(&torrent::set_file_priority, index, priority);
	}

	int torrent_handle::file_priority(int index) const
	{
		return sync_call_ret<int>(0, &torrent::file_priority, index);
	}

	void torrent_handle::prioritize_files(std::vector<int> const& files) const
	{
		async_call(&torrent::prioritize_files, files);
	}

	std::vector<int> torrent_handle::file_priorities() const
	{
		std::vector<int> ret;
		auto retp = &ret;
		sync_call(&torrent::file_priorities, retp);
		return ret;
	}

#ifndef TORRENT_NO_DEPRECATE
// ============ start deprecation ===============

	int torrent_handle::get_peer_upload_limit(tcp::endpoint) const { return -1; }
	int torrent_handle::get_peer_download_limit(tcp::endpoint) const { return -1; }
	void torrent_handle::set_peer_upload_limit(tcp::endpoint, int /* limit */) const {}
	void torrent_handle::set_peer_download_limit(tcp::endpoint, int /* limit */) const {}
	void torrent_handle::set_ratio(float) const {}
	void torrent_handle::use_interface(const char* net_interface) const
	{
		async_call(&torrent::use_interface, std::string(net_interface));
	}

#if !TORRENT_NO_FPU
	void torrent_handle::file_progress(std::vector<float>& progress) const
	{
		sync_call(&torrent::file_progress_float, std::ref(progress));
	}
#endif

	bool torrent_handle::is_seed() const
	{
		return sync_call_ret<bool>(false, &torrent::is_seed);
	}

	bool torrent_handle::is_finished() const
	{
		return sync_call_ret<bool>(false, &torrent::is_finished);
	}

	bool torrent_handle::is_paused() const
	{
		return sync_call_ret<bool>(false, &torrent::is_torrent_paused);
	}

	bool torrent_handle::is_sequential_download() const
	{
		return sync_call_ret<bool>(false, &torrent::is_sequential_download);
	}

	bool torrent_handle::is_auto_managed() const
	{
		return sync_call_ret<bool>(false, &torrent::is_auto_managed);
	}

	bool torrent_handle::has_metadata() const
	{
		return sync_call_ret<bool>(false, &torrent::valid_metadata);
	}

	void torrent_handle::filter_piece(int index, bool filter) const
	{
		async_call(&torrent::filter_piece, index, filter);
	}

	void torrent_handle::filter_pieces(std::vector<bool> const& pieces) const
	{
		async_call(&torrent::filter_pieces, pieces);
	}

	bool torrent_handle::is_piece_filtered(int index) const
	{
		return sync_call_ret<bool>(false, &torrent::is_piece_filtered, index);
	}

	std::vector<bool> torrent_handle::filtered_pieces() const
	{
		std::vector<bool> ret;
		auto retr = std::ref(ret);
		sync_call(&torrent::filtered_pieces, retr);
		return ret;
	}

	void torrent_handle::filter_files(std::vector<bool> const& files) const
	{
		auto filesr= std::ref(files);
		async_call(&torrent::filter_files, filesr);
	}

	bool torrent_handle::super_seeding() const
	{
		return sync_call_ret<bool>(false, &torrent::super_seeding);
	}

// ============ end deprecation ===============
#endif

	std::vector<announce_entry> torrent_handle::trackers() const
	{
		static const std::vector<announce_entry> empty;
		return sync_call_ret<std::vector<announce_entry>>(empty, &torrent::trackers);
	}

	void torrent_handle::add_url_seed(std::string const& url) const
	{
		async_call(&torrent::add_web_seed, url, web_seed_entry::url_seed
			, std::string(), web_seed_entry::headers_t());
	}

	void torrent_handle::remove_url_seed(std::string const& url) const
	{
		async_call(&torrent::remove_web_seed, url, web_seed_entry::url_seed);
	}

	std::set<std::string> torrent_handle::url_seeds() const
	{
		static const std::set<std::string> empty;
		return sync_call_ret<std::set<std::string>>(empty, &torrent::web_seeds, web_seed_entry::url_seed);
	}

	void torrent_handle::add_http_seed(std::string const& url) const
	{
		async_call(&torrent::add_web_seed, url, web_seed_entry::http_seed
			, std::string(), web_seed_entry::headers_t());
	}

	void torrent_handle::remove_http_seed(std::string const& url) const
	{
		async_call(&torrent::remove_web_seed, url, web_seed_entry::http_seed);
	}

	std::set<std::string> torrent_handle::http_seeds() const
	{
		static const std::set<std::string> empty;
		return sync_call_ret<std::set<std::string>>(empty, &torrent::web_seeds, web_seed_entry::http_seed);
	}

	void torrent_handle::replace_trackers(
		std::vector<announce_entry> const& urls) const
	{
		async_call(&torrent::replace_trackers, urls);
	}

	void torrent_handle::add_tracker(announce_entry const& url) const
	{
		async_call(&torrent::add_tracker, url);
	}

	void torrent_handle::add_piece(int piece, char const* data, int flags) const
	{
		sync_call(&torrent::add_piece, piece, data, flags);
	}

	void torrent_handle::read_piece(int piece) const
	{
		async_call(&torrent::read_piece, piece);
	}

	bool torrent_handle::have_piece(int piece) const
	{
		return sync_call_ret<bool>(false, &torrent::have_piece, piece);
	}

	storage_interface* torrent_handle::get_storage_impl() const
	{
		return sync_call_ret<storage_interface*>(nullptr, &torrent::get_storage);
	}

	bool torrent_handle::is_valid() const
	{
		return !m_torrent.expired();
	}

	std::shared_ptr<const torrent_info> torrent_handle::torrent_file() const
	{
		return sync_call_ret<std::shared_ptr<const torrent_info>>(
			std::shared_ptr<const torrent_info>(), &torrent::get_torrent_copy);
	}

#ifndef TORRENT_NO_DEPRECATE
	// this function should either be removed, or return
	// reference counted handle to the torrent_info which
	// forces the torrent to stay loaded while the client holds it
	torrent_info const& torrent_handle::get_torrent_info() const
	{
		static std::shared_ptr<const torrent_info> holder[4];
		static int cursor = 0;
		static std::mutex holder_mutex;

		std::shared_ptr<const torrent_info> r = torrent_file();

		std::lock_guard<std::mutex> l(holder_mutex);
		holder[cursor++] = r;
		cursor = cursor % (sizeof(holder) / sizeof(holder[0]));
		return *r;
	}

	entry torrent_handle::write_resume_data() const
	{
		entry ret(entry::dictionary_t);
		auto retr = std::ref(ret);
		sync_call(&torrent::write_resume_data, retr);
		return ret;
	}

	std::string torrent_handle::save_path() const
	{
		return sync_call_ret<std::string>("", &torrent::save_path);
	}

	std::string torrent_handle::name() const
	{
		return sync_call_ret<std::string>("", &torrent::name);
	}

#endif

	void torrent_handle::connect_peer(tcp::endpoint const& adr, int source, int flags) const
	{
		async_call(&torrent::add_peer, adr, source, flags);
	}

#ifndef TORRENT_NO_DEPRECATE
	void torrent_handle::force_reannounce(
		boost::posix_time::time_duration duration) const
	{
		async_call(&torrent::force_tracker_request, aux::time_now()
			+ seconds(duration.total_seconds()), -1);
	}

	void torrent_handle::file_status(std::vector<pool_file_status>& status) const
	{
		status.clear();

		std::shared_ptr<torrent> t = m_torrent.lock();
		if (!t || !t->has_storage()) return;
		session_impl& ses = static_cast<session_impl&>(t->session());
		status = ses.disk_thread().files().get_status(&t->storage());
	}
#endif

	void torrent_handle::force_dht_announce() const
	{
#ifndef TORRENT_DISABLE_DHT
		async_call(&torrent::dht_announce);
#endif
	}

	void torrent_handle::force_reannounce(int s, int idx) const
	{
		async_call(&torrent::force_tracker_request, aux::time_now() + seconds(s), idx);
	}

	std::vector<pool_file_status> torrent_handle::file_status() const
	{
		std::shared_ptr<torrent> t = m_torrent.lock();
		if (!t || !t->has_storage()) return {};
		session_impl& ses = static_cast<session_impl&>(t->session());
		return ses.disk_thread().files().get_status(&t->storage());
	}

	void torrent_handle::scrape_tracker(int idx) const
	{
		async_call(&torrent::scrape_tracker, idx, true);
	}

	void torrent_handle::super_seeding(bool on) const
	{
		async_call(&torrent::set_super_seeding, on);
	}

#ifndef TORRENT_NO_DEPRECATE
	void torrent_handle::get_full_peer_list(std::vector<peer_list_entry>& v) const
	{
		auto vp = &v;
		sync_call(&torrent::get_full_peer_list, vp);
	}
#endif

	void torrent_handle::get_peer_info(std::vector<peer_info>& v) const
	{
		auto vp = &v;
		sync_call(&torrent::get_peer_info, vp);
	}

	void torrent_handle::get_download_queue(std::vector<partial_piece_info>& queue) const
	{
		auto queuep = &queue;
		sync_call(&torrent::get_download_queue, queuep);
	}

	void torrent_handle::set_piece_deadline(int index, int deadline, int flags) const
	{
		async_call(&torrent::set_piece_deadline, index, deadline, flags);
	}

	void torrent_handle::reset_piece_deadline(int index) const
	{
		async_call(&torrent::reset_piece_deadline, index);
	}

	void torrent_handle::clear_piece_deadlines() const
	{
		async_call(&torrent::clear_time_critical);
	}

	std::shared_ptr<torrent> torrent_handle::native_handle() const
	{
		return m_torrent.lock();
	}

	std::size_t hash_value(torrent_handle const& th)
	{
		// using the locked shared_ptr value as hash doesn't work
		// for expired weak_ptrs. So, we're left with a hack
		return std::size_t(*reinterpret_cast<void* const*>(&th.m_torrent));
	}
}
