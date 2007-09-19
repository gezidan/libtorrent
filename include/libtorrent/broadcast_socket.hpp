/*

Copyright (c) 2007, Arvid Norberg
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

#ifndef TORRENT_BROADCAST_SOCKET_HPP_INCLUDED
#define TORRENT_BROADCAST_SOCKET_HPP_INCLUDED

#include "libtorrent/socket.hpp"
#include <boost/shared_ptr.hpp>
#include <boost/function.hpp>
#include <list>

namespace libtorrent
{

	bool is_local(address const& a);
	address_v4 guess_local_address(asio::io_service&);

	typedef boost::function<void(udp::endpoint const& from
		, char* buffer, int size)> receive_handler_t;

	class broadcast_socket
	{
	public:
		broadcast_socket(asio::io_service& ios, udp::endpoint const& multicast_endpoint
			, receive_handler_t const& handler, bool loopback = true);
		~broadcast_socket() { close(); }

		void send(char const* buffer, int size, asio::error_code& ec);
		void close();

	private:

		struct socket_entry
		{
			socket_entry(boost::shared_ptr<datagram_socket> const& s): socket(s) {}
			boost::shared_ptr<datagram_socket> socket;
			char buffer[1024];
			udp::endpoint remote;
		};
	
		void on_receive(socket_entry* s, asio::error_code const& ec
			, std::size_t bytes_transferred);

		std::list<socket_entry> m_sockets;
		udp::endpoint m_multicast_endpoint;
		receive_handler_t m_on_receive;
		
	};
}
	
#endif
