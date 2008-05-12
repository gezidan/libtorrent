/*

Copyright (c) 2003, Arvid Norberg
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

#include "libtorrent/pch.hpp"

#include <ctime>
#include <iostream>
#include <fstream>
#include <iomanip>
#include <iterator>
#include <algorithm>
#include <set>

#ifdef _MSC_VER
#pragma warning(push, 1)
#endif

#include <boost/lexical_cast.hpp>
#include <boost/filesystem/path.hpp>
#include <boost/filesystem.hpp>
#include <boost/next_prior.hpp>
#include <boost/bind.hpp>

#ifdef _MSC_VER
#pragma warning(pop)
#endif

#include "libtorrent/torrent_info.hpp"
#include "libtorrent/bencode.hpp"
#include "libtorrent/hasher.hpp"
#include "libtorrent/entry.hpp"

namespace gr = boost::gregorian;

using namespace libtorrent;

namespace
{
	
	namespace fs = boost::filesystem;

	void convert_to_utf8(std::string& str, unsigned char chr)
	{
		str += 0xc0 | ((chr & 0xff) >> 6);
		str += 0x80 | (chr & 0x3f);
	}

	void verify_encoding(file_entry& target)
	{
		std::string tmp_path;
		std::string file_path = target.path.string();
		bool valid_encoding = true;
		for (std::string::iterator i = file_path.begin()
			, end(file_path.end()); i != end; ++i)
		{
			// valid ascii-character
			if ((*i & 0x80) == 0)
			{
				tmp_path += *i;
				continue;
			}
			
			if (std::distance(i, end) < 2)
			{
				convert_to_utf8(tmp_path, *i);
				valid_encoding = false;
				continue;
			}
			
			// valid 2-byte utf-8 character
			if ((i[0] & 0xe0) == 0xc0
				&& (i[1] & 0xc0) == 0x80)
			{
				tmp_path += i[0];
				tmp_path += i[1];
				i += 1;
				continue;
			}

			if (std::distance(i, end) < 3)
			{
				convert_to_utf8(tmp_path, *i);
				valid_encoding = false;
				continue;
			}

			// valid 3-byte utf-8 character
			if ((i[0] & 0xf0) == 0xe0
				&& (i[1] & 0xc0) == 0x80
				&& (i[2] & 0xc0) == 0x80)
			{
				tmp_path += i[0];
				tmp_path += i[1];
				tmp_path += i[2];
				i += 2;
				continue;
			}

			if (std::distance(i, end) < 4)
			{
				convert_to_utf8(tmp_path, *i);
				valid_encoding = false;
				continue;
			}

			// valid 4-byte utf-8 character
			if ((i[0] & 0xf0) == 0xe0
				&& (i[1] & 0xc0) == 0x80
				&& (i[2] & 0xc0) == 0x80
				&& (i[3] & 0xc0) == 0x80)
			{
				tmp_path += i[0];
				tmp_path += i[1];
				tmp_path += i[2];
				tmp_path += i[3];
				i += 3;
				continue;
			}

			convert_to_utf8(tmp_path, *i);
			valid_encoding = false;
		}
		// the encoding was not valid utf-8
		// save the original encoding and replace the
		// commonly used path with the correctly
		// encoded string
		if (!valid_encoding)
		{
			target.orig_path.reset(new fs::path(target.path));
			target.path = tmp_path;
		}
	}

	bool extract_single_file(entry const& dict, file_entry& target
		, std::string const& root_dir)
	{
		entry const* length = dict.find_key("length");
		if (length == 0 || length->type() != entry::int_t)
			return false;
		target.size = length->integer();
		target.path = root_dir;
		target.file_base = 0;

		// prefer the name.utf-8
		// because if it exists, it is more
		// likely to be correctly encoded

		const entry::list_type* list = 0;
		entry const* p8 = dict.find_key("path.utf-8");
		if (p8 != 0 && p8->type() == entry::list_t)
		{
			list = &p8->list();
		}
		else
		{
			entry const* p = dict.find_key("path");
			if (p == 0 || p->type() != entry::list_t)
				return false;
			list = &p->list();
		}

		for (entry::list_type::const_iterator i = list->begin();
			i != list->end(); ++i)
		{
			if (i->type() != entry::string_t)
				return false;
			if (i->string() != "..")
				target.path /= i->string();
		}
		verify_encoding(target);
		if (target.path.is_complete())
			return false;
		return true;
	}

	bool extract_files(const entry::list_type& list, std::vector<file_entry>& target
		, std::string const& root_dir)
	{
		size_type offset = 0;
		for (entry::list_type::const_iterator i = list.begin(); i != list.end(); ++i)
		{
			target.push_back(file_entry());
			if (!extract_single_file(*i, target.back(), root_dir))
				return false;
			target.back().offset = offset;
			offset += target.back().size;
		}
		return true;
	}
/*
	void remove_dir(fs::path& p)
	{
		TORRENT_ASSERT(p.begin() != p.end());
		path tmp;
		for (path::iterator i = boost::next(p.begin()); i != p.end(); ++i)
			tmp /= *i;
		p = tmp;
	}
*/
}

namespace libtorrent
{

	// standard constructor that parses a torrent file
	torrent_info::torrent_info(const entry& torrent_file)
		: m_num_pieces(0)
		, m_creation_date(pt::ptime(pt::not_a_date_time))
		, m_multifile(false)
		, m_private(false)
		, m_extra_info(entry::dictionary_t)
#ifndef NDEBUG
		, m_half_metadata(false)
#endif
	{
		std::string error;
#ifndef BOOST_NO_EXCEPTIONS
		if (!read_torrent_info(torrent_file, error))
			throw invalid_torrent_file();
#else
		read_torrent_info(torrent_file, error);
#endif
	}

	// constructor used for creating new torrents
	// will not contain any hashes, comments, creation date
	// just the necessary to use it with piece manager
	// used for torrents with no metadata
	torrent_info::torrent_info(sha1_hash const& info_hash)
		: m_piece_length(0)
		, m_total_size(0)
		, m_num_pieces(0)
		, m_info_hash(info_hash)
		, m_name()
		, m_creation_date(pt::second_clock::universal_time())
		, m_multifile(false)
		, m_private(false)
		, m_extra_info(entry::dictionary_t)
#ifndef NDEBUG
		, m_half_metadata(false)
#endif
	{
	}

	torrent_info::torrent_info()
		: m_piece_length(0)
		, m_total_size(0)
		, m_num_pieces(0)
		, m_info_hash(0)
		, m_name()
		, m_creation_date(pt::second_clock::universal_time())
		, m_multifile(false)
		, m_private(false)
		, m_extra_info(entry::dictionary_t)
#ifndef NDEBUG
		, m_half_metadata(false)
#endif
	{
	}

	torrent_info::torrent_info(char const* filename)
		: m_num_pieces(0)
		, m_creation_date(pt::ptime(pt::not_a_date_time))
		, m_multifile(false)
		, m_private(false)
		, m_extra_info(entry::dictionary_t)
#ifndef NDEBUG
		, m_half_metadata(false)
#endif
	{
		size_type s = fs::file_size(fs::path(filename));
		// don't load torrent files larger than 2 MB
		if (s > 2000000) return;
		std::vector<char> buf(s);
		std::ifstream f(filename);
		f.read(&buf[0], s);
		/*
		lazy_entry e;
		int ret = lazy_bdecode(&buf[0], &buf[0] + buf.size(), e);
		if (ret != 0) return;
		*/
		entry e = bdecode(&buf[0], &buf[0] + buf.size());
		std::string error;
#ifndef BOOST_NO_EXCEPTIONS
		if (!read_torrent_info(e, error))
			throw invalid_torrent_file();
#else
		read_torrent_info(e, error);
#endif
	}

	torrent_info::~torrent_info()
	{}

	void torrent_info::swap(torrent_info& ti)
	{
		using std::swap;
		m_urls.swap(ti.m_urls);
		m_url_seeds.swap(ti.m_url_seeds);
		swap(m_piece_length, ti.m_piece_length);
		m_piece_hash.swap(ti.m_piece_hash);
		m_files.swap(ti.m_files);
		m_nodes.swap(ti.m_nodes);
		swap(m_num_pieces, ti.m_num_pieces);
		swap(m_info_hash, ti.m_info_hash);
		m_name.swap(ti.m_name);
		swap(m_creation_date, ti.m_creation_date);
		m_comment.swap(ti.m_comment);
		m_created_by.swap(ti.m_created_by);
		swap(m_multifile, ti.m_multifile);
		swap(m_private, ti.m_private);
		m_extra_info.swap(ti.m_extra_info);
#ifndef NDEBUG
		swap(m_half_metadata, ti.m_half_metadata);
#endif
	}

	void torrent_info::set_piece_size(int size)
	{
		// make sure the size is an even power of 2
#ifndef NDEBUG
		for (int i = 0; i < 32; ++i)
		{
			if (size & (1 << i))
			{
				TORRENT_ASSERT((size & ~(1 << i)) == 0);
				break;
			}
		}
#endif
		TORRENT_ASSERT(!m_half_metadata);
		m_piece_length = size;

		m_num_pieces = static_cast<int>(
			(m_total_size + m_piece_length - 1) / m_piece_length);
		int old_num_pieces = static_cast<int>(m_piece_hash.size());

		m_piece_hash.resize(m_num_pieces);
		for (int i = old_num_pieces; i < m_num_pieces; ++i)
		{
			m_piece_hash[i].clear();
		}
	}

	bool torrent_info::parse_info_section(entry const& info, std::string& error)
	{
		if (info.type() != entry::dictionary_t)
		{
			error = "'info' entry is not a dictionary";
			return false;
		}

		// encode the info-field in order to calculate it's sha1-hash
		std::vector<char> buf;
		bencode(std::back_inserter(buf), info);
		hasher h;
		h.update(&buf[0], (int)buf.size());
		m_info_hash = h.final();

		// extract piece length
		entry const* piece_length = info.find_key("piece length");
		if (piece_length == 0 || piece_length->type() != entry::int_t)
		{
			error = "invalid or missing 'piece length' entry in torrent file";
			return false;
		}
		m_piece_length = int(piece_length->integer());
		if (m_piece_length <= 0)
		{
			error = "invalid torrent. piece length <= 0";
			return false;
		}

		// extract file name (or the directory name if it's a multifile libtorrent)
		entry const* e = info.find_key("name.utf-8");
		if (e && e->type() == entry::string_t)
		{ m_name = e->string(); }
		else
		{
			entry const* e = info.find_key("name");
			if (e == 0 || e->type() != entry::string_t)
			{
				error = "invalid name in torrent file";
				return false;
			}
			m_name = e->string();
		}
		
		fs::path tmp = m_name;
		if (tmp.is_complete())
		{
			m_name = tmp.leaf();
		}
		else if (tmp.has_branch_path())
		{
			fs::path p;
			for (fs::path::iterator i = tmp.begin()
				, end(tmp.end()); i != end; ++i)
			{
				if (*i == "." || *i == "..") continue;
				p /= *i;
			}
			m_name = p.string();
		}
		if (m_name == ".." || m_name == ".")
		{
		
			error = "invalid 'name' of torrent (possible exploit attempt)";
			return false;
		}
	
		// extract file list
		entry const* i = info.find_key("files");
		if (i == 0)
		{
			entry const* length = info.find_key("length");
			if (length == 0 || length->type() != entry::int_t)
			{
				error = "invalid length of torrent";
				return false;
			}
			// if there's no list of files, there has to be a length
			// field.
			file_entry e;
			e.path = m_name;
			e.offset = 0;
			e.size = length->integer();
			m_files.push_back(e);
		}
		else
		{
			if (!extract_files(i->list(), m_files, m_name))
			{
				error = "failed to parse files from torrent file";
				return false;
			}
			m_multifile = true;
		}

		// calculate total size of all pieces
		m_total_size = 0;
		for (std::vector<file_entry>::iterator i = m_files.begin(); i != m_files.end(); ++i)
			m_total_size += i->size;

		// extract sha-1 hashes for all pieces
		// we want this division to round upwards, that's why we have the
		// extra addition

		entry const* pieces_ent = info.find_key("pieces");
		if (pieces_ent == 0 || pieces_ent->type() != entry::string_t)
		{
			error = "invalid or missing 'pieces' entry in torrent file";
			return false;
		}
		
		m_num_pieces = static_cast<int>((m_total_size + m_piece_length - 1) / m_piece_length);
		m_piece_hash.resize(m_num_pieces);
		const std::string& hash_string = pieces_ent->string();

		if ((int)hash_string.length() != m_num_pieces * 20)
		{
			error = "incorrect number of piece hashes in torrent file";
			return false;
		}

		for (int i = 0; i < m_num_pieces; ++i)
			std::copy(
				hash_string.begin() + i*20
				, hash_string.begin() + (i+1)*20
				, m_piece_hash[i].begin());

		for (entry::dictionary_type::const_iterator i = info.dict().begin()
			, end(info.dict().end()); i != end; ++i)
		{
			if (i->first == "pieces"
				|| i->first == "piece length"
				|| i->first == "length")
				continue;
			m_extra_info[i->first] = i->second;
		}

		if (entry const* priv = info.find_key("private"))
		{
			if (priv->type() != entry::int_t
				|| priv->integer() != 0)
			{
				// this key exists and it's not 0.
				// consider the torrent private
				m_private = true;
			}
		}

#ifndef NDEBUG
		std::vector<char> info_section_buf;
		entry gen_info_section = create_info_metadata();
		bencode(std::back_inserter(info_section_buf), gen_info_section);
		TORRENT_ASSERT(hasher(&info_section_buf[0], info_section_buf.size()).final()
			== m_info_hash);
#endif
		return true;
	}

	// extracts information from a libtorrent file and fills in the structures in
	// the torrent object
	bool torrent_info::read_torrent_info(const entry& torrent_file, std::string& error)
	{
		if (torrent_file.type() != entry::dictionary_t)
		{
			error = "torrent file is not a dictionary";
			return false;
		}

		// extract the url of the tracker
		entry const* i = torrent_file.find_key("announce-list");
		if (i && i->type() == entry::list_t)
		{
			const entry::list_type& l = i->list();
			for (entry::list_type::const_iterator j = l.begin(); j != l.end(); ++j)
			{
				if (j->type() != entry::list_t) break;
				const entry::list_type& ll = j->list();
				for (entry::list_type::const_iterator k = ll.begin(); k != ll.end(); ++k)
				{
					if (k->type() != entry::string_t) break;
					announce_entry e(k->string());
					e.tier = (int)std::distance(l.begin(), j);
					m_urls.push_back(e);
				}
			}

			// shuffle each tier
			std::vector<announce_entry>::iterator start = m_urls.begin();
			std::vector<announce_entry>::iterator stop;
			int current_tier = m_urls.front().tier;
			for (stop = m_urls.begin(); stop != m_urls.end(); ++stop)
			{
				if (stop->tier != current_tier)
				{
					std::random_shuffle(start, stop);
					start = stop;
					current_tier = stop->tier;
				}
			}
			std::random_shuffle(start, stop);
		}
		
		entry const* announce = torrent_file.find_key("announce");
		if (m_urls.empty() && announce && announce->type() == entry::string_t)
		{
			m_urls.push_back(announce_entry(announce->string()));
		}

		entry const* nodes = torrent_file.find_key("nodes");
		if (nodes && nodes->type() == entry::list_t)
		{
			entry::list_type const& list = nodes->list();
			for (entry::list_type::const_iterator i(list.begin())
				, end(list.end()); i != end; ++i)
			{
				if (i->type() != entry::list_t) continue;
				entry::list_type const& l = i->list();
				entry::list_type::const_iterator iter = l.begin();
				if (l.size() < 1) continue;
				if (iter->type() != entry::string_t) continue;
				std::string const& hostname = iter->string();
				++iter;
				int port = 6881;
				if (iter->type() != entry::int_t) continue;
				if (l.end() != iter) port = int(iter->integer());
				m_nodes.push_back(std::make_pair(hostname, port));
			}
		}

		// extract creation date
		entry const* creation_date = torrent_file.find_key("creation date");
		if (creation_date && creation_date->type() == entry::int_t)
		{
			m_creation_date = pt::ptime(gr::date(1970, gr::Jan, 1))
				+ pt::seconds(long(creation_date->integer()));
		}

		// if there are any url-seeds, extract them
		entry const* url_seeds = torrent_file.find_key("url-list");
		if (url_seeds && url_seeds->type() == entry::string_t)
		{
			m_url_seeds.push_back(url_seeds->string());
		}
		else if (url_seeds && url_seeds->type() == entry::list_t)
		{
			entry::list_type const& l = url_seeds->list();
			for (entry::list_type::const_iterator i = l.begin();
				i != l.end(); ++i)
			{
				if (i->type() != entry::string_t) continue;
				m_url_seeds.push_back(i->string());
			}
		}

		// extract comment
		if (entry const* e = torrent_file.find_key("comment.utf-8"))
		{ m_comment = e->string(); }
		else if (entry const* e = torrent_file.find_key("comment"))
		{ m_comment = e->string(); }
	
		if (entry const* e = torrent_file.find_key("created by.utf-8"))
		{ m_created_by = e->string(); }
		else if (entry const* e = torrent_file.find_key("created by"))
		{ m_created_by = e->string(); }

		entry const* info = torrent_file.find_key("info");
		if (info == 0 || info->type() != entry::dictionary_t)
		{
			error = "missing or invalid 'info' section in torrent file";
			return false;
		}
		return parse_info_section(*info, error);
	}

	boost::optional<pt::ptime>
	torrent_info::creation_date() const
	{
		if (m_creation_date != pt::ptime(gr::date(pt::not_a_date_time)))
		{
			return boost::optional<pt::ptime>(m_creation_date);
		}
		return boost::optional<pt::ptime>();
	}

	void torrent_info::add_tracker(std::string const& url, int tier)
	{
		announce_entry e(url);
		e.tier = tier;
		m_urls.push_back(e);

		using boost::bind;
		std::sort(m_urls.begin(), m_urls.end(), boost::bind<bool>(std::less<int>()
			, bind(&announce_entry::tier, _1), bind(&announce_entry::tier, _2)));
	}

	void torrent_info::add_file(fs::path file, size_type size)
	{
//		TORRENT_ASSERT(file.begin() != file.end());

		if (!file.has_branch_path())
		{
			// you have already added at least one file with a
			// path to the file (branch_path), which means that
			// all the other files need to be in the same top
			// directory as the first file.
			TORRENT_ASSERT(m_files.empty());
			TORRENT_ASSERT(!m_multifile);
			m_name = file.string();
		}
		else
		{
#ifndef NDEBUG
			if (!m_files.empty())
				TORRENT_ASSERT(m_name == *file.begin());
#endif
			m_multifile = true;
			m_name = *file.begin();
		}

		file_entry e;
		e.path = file;
		e.size = size;
		e.offset = m_files.empty() ? 0 : m_files.back().offset
			+ m_files.back().size;
		m_files.push_back(e);

		m_total_size += size;
		
		if (m_piece_length == 0)
			m_piece_length = 256 * 1024;

		m_num_pieces = static_cast<int>(
			(m_total_size + m_piece_length - 1) / m_piece_length);
		int old_num_pieces = static_cast<int>(m_piece_hash.size());

		m_piece_hash.resize(m_num_pieces);
		if (m_num_pieces > old_num_pieces)
			std::for_each(m_piece_hash.begin() + old_num_pieces
				, m_piece_hash.end(), boost::bind(&sha1_hash::clear, _1));
	}

	void torrent_info::add_url_seed(std::string const& url)
	{
		m_url_seeds.push_back(url);
	}

	void torrent_info::set_comment(char const* str)
	{
		m_comment = str;
	}

	void torrent_info::set_creator(char const* str)
	{
		m_created_by = str;
	}

	entry torrent_info::create_info_metadata() const
	{
		// you have to add files to the torrent first
		TORRENT_ASSERT(!m_files.empty());
	
		entry info(m_extra_info);

		if (!info.find_key("name"))
			info["name"] = m_name;

		if (m_private) info["private"] = 1;

		if (!m_multifile)
		{
			info["length"] = m_files.front().size;
		}
		else
		{
			if (!info.find_key("files"))
			{
				entry& files = info["files"];

				for (std::vector<file_entry>::const_iterator i = m_files.begin();
					i != m_files.end(); ++i)
				{
					files.list().push_back(entry());
					entry& file_e = files.list().back();
					file_e["length"] = i->size;
					entry& path_e = file_e["path"];

					fs::path const* file_path;
					if (i->orig_path) file_path = &(*i->orig_path);
					else file_path = &i->path;
					TORRENT_ASSERT(file_path->has_branch_path());
					TORRENT_ASSERT(*file_path->begin() == m_name);

					for (fs::path::iterator j = boost::next(file_path->begin());
						j != file_path->end(); ++j)
					{
						path_e.list().push_back(entry(*j));
					}
				}
			}
		}

		info["piece length"] = piece_length();
		entry& pieces = info["pieces"];

		std::string& p = pieces.string();

		for (std::vector<sha1_hash>::const_iterator i = m_piece_hash.begin();
			i != m_piece_hash.end(); ++i)
		{
			p.append((char*)i->begin(), (char*)i->end());
		}

		return info;
	}

	entry torrent_info::create_torrent() const
	{
		TORRENT_ASSERT(m_piece_length > 0);

		if (m_files.empty())
		{
			// TODO: throw something here
			// throw
			return entry();
		}

		entry dict;

		if (!m_urls.empty())
			dict["announce"] = m_urls.front().url;
		
		if (!m_nodes.empty())
		{
			entry& nodes = dict["nodes"];
			entry::list_type& nodes_list = nodes.list();
			for (nodes_t::const_iterator i = m_nodes.begin()
				, end(m_nodes.end()); i != end; ++i)
			{
				entry::list_type node;
				node.push_back(entry(i->first));
				node.push_back(entry(i->second));
				nodes_list.push_back(entry(node));
			}
		}

		if (m_urls.size() > 1)
		{
			entry trackers(entry::list_t);
			entry tier(entry::list_t);
			int current_tier = m_urls.front().tier;
			for (std::vector<announce_entry>::const_iterator i = m_urls.begin();
				i != m_urls.end(); ++i)
			{
				if (i->tier != current_tier)
				{
					current_tier = i->tier;
					trackers.list().push_back(tier);
					tier.list().clear();
				}
				tier.list().push_back(entry(i->url));
			}
			trackers.list().push_back(tier);
			dict["announce-list"] = trackers;
		}

		if (!m_comment.empty())
			dict["comment"] = m_comment;

		dict["creation date"] =
			(m_creation_date - pt::ptime(gr::date(1970, gr::Jan, 1))).total_seconds();

		if (!m_created_by.empty())
			dict["created by"] = m_created_by;
			
		if (!m_url_seeds.empty())
		{
			if (m_url_seeds.size() == 1)
			{
				dict["url-list"] = m_url_seeds.front();
			}
			else
			{
				entry& list = dict["url-list"];
				for (std::vector<std::string>::const_iterator i
					= m_url_seeds.begin(); i != m_url_seeds.end(); ++i)
				{
					list.list().push_back(entry(*i));
				}
			}
		}

		dict["info"] = create_info_metadata();

		entry const& info_section = dict["info"];
		std::vector<char> buf;
		bencode(std::back_inserter(buf), info_section);
		m_info_hash = hasher(&buf[0], buf.size()).final();

		return dict;
	}

	void torrent_info::set_hash(int index, const sha1_hash& h)
	{
		TORRENT_ASSERT(index >= 0);
		TORRENT_ASSERT(index < (int)m_piece_hash.size());
		m_piece_hash[index] = h;
	}

	void torrent_info::convert_file_names()
	{
		TORRENT_ASSERT(false);
	}

	void torrent_info::seed_free()
	{
		std::vector<std::string>().swap(m_url_seeds);
		nodes_t().swap(m_nodes);
		std::vector<sha1_hash>().swap(m_piece_hash);
#ifndef NDEBUG
		m_half_metadata = true;
#endif
	}

// ------- start deprecation -------

	void torrent_info::print(std::ostream& os) const
	{
		os << "trackers:\n";
		for (std::vector<announce_entry>::const_iterator i = trackers().begin();
			i != trackers().end(); ++i)
		{
			os << i->tier << ": " << i->url << "\n";
		}
		if (!m_comment.empty())
			os << "comment: " << m_comment << "\n";
//		if (m_creation_date != pt::ptime(gr::date(pt::not_a_date_time)))
//			os << "creation date: " << to_simple_string(m_creation_date) << "\n";
		os << "private: " << (m_private?"yes":"no") << "\n";
		os << "number of pieces: " << num_pieces() << "\n";
		os << "piece length: " << piece_length() << "\n";
		os << "files:\n";
		for (file_iterator i = begin_files(); i != end_files(); ++i)
			os << "  " << std::setw(11) << i->size << "  " << i->path.string() << "\n";
	}

// ------- end deprecation -------

	int torrent_info::piece_size(int index) const
	{
		TORRENT_ASSERT(index >= 0 && index < num_pieces());
		if (index == num_pieces()-1)
		{
			int size = int(total_size()
				- (num_pieces() - 1) * piece_length());
			TORRENT_ASSERT(size > 0);
			TORRENT_ASSERT(size <= piece_length());
			return int(size);
		}
		else
			return piece_length();
	}

	void torrent_info::add_node(std::pair<std::string, int> const& node)
	{
		m_nodes.push_back(node);
	}

	bool torrent_info::remap_files(std::vector<file_entry> const& map)
	{
		size_type offset = 0;
		m_remapped_files.resize(map.size());

		for (int i = 0; i < int(map.size()); ++i)
		{
			file_entry& fe = m_remapped_files[i];
			fe.path = map[i].path;
			fe.offset = offset;
			fe.size = map[i].size;
			fe.file_base = map[i].file_base;
			fe.orig_path.reset();
			offset += fe.size;
		}
		if (offset != total_size())
		{
			m_remapped_files.clear();
			return false;
		}

#ifndef NDEBUG
		std::vector<file_entry> map2(m_remapped_files);
		std::sort(map2.begin(), map2.end()
			, bind(&file_entry::file_base, _1) < bind(&file_entry::file_base, _2));
		std::stable_sort(map2.begin(), map2.end()
			, bind(&file_entry::path, _1) < bind(&file_entry::path, _2));
		fs::path last_path;
		size_type last_end = 0;
		for (std::vector<file_entry>::iterator i = map2.begin(), end(map2.end());
			i != end; ++i)
		{
			if (last_path == i->path)
			{
				assert(last_end <= i->file_base);
			}
			last_end = i->file_base + i->size;
			last_path = i->path;
		}
#endif

		return true;
	}

	std::vector<file_slice> torrent_info::map_block(int piece, size_type offset
		, int size_, bool storage) const
	{
		TORRENT_ASSERT(num_files() > 0);
		std::vector<file_slice> ret;

		size_type start = piece * (size_type)m_piece_length + offset;
		size_type size = size_;
		TORRENT_ASSERT(start + size <= m_total_size);

		// find the file iterator and file offset
		// TODO: make a vector that can map piece -> file index in O(1)
		size_type file_offset = start;
		std::vector<file_entry>::const_iterator file_iter;

		int counter = 0;
		for (file_iter = begin_files(storage);; ++counter, ++file_iter)
		{
			TORRENT_ASSERT(file_iter != end_files(storage));
			if (file_offset < file_iter->size)
			{
				file_slice f;
				f.file_index = counter;
				f.offset = file_offset + file_iter->file_base;
				f.size = (std::min)(file_iter->size - file_offset, (size_type)size);
				size -= f.size;
				file_offset += f.size;
				ret.push_back(f);
			}
			
			TORRENT_ASSERT(size >= 0);
			if (size <= 0) break;

			file_offset -= file_iter->size;
		}
		return ret;
	}
	
	peer_request torrent_info::map_file(int file_index, size_type file_offset
		, int size, bool storage) const
	{
		TORRENT_ASSERT(file_index < num_files(storage));
		TORRENT_ASSERT(file_index >= 0);
		size_type offset = file_offset + file_at(file_index, storage).offset;

		peer_request ret;
		ret.piece = int(offset / piece_length());
		ret.start = int(offset - ret.piece * piece_length());
		ret.length = size;
		return ret;
	}

}

