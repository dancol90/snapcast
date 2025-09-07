/***
    This file is part of snapcast
    Copyright (C) 2014-2025  Johannes Pohl

    This program is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.
***/

// prototype/interface header file
#include "raop_player.hpp"

// local headers
#include "common/aixlog.hpp"
#include "common/snap_exception.hpp"
#include "common/str_compat.hpp"
#include "common/utils/string_utils.hpp"

// 3rd party headers

// standard headers

using namespace std;

#include <raop_client.h>

namespace player
{

static constexpr auto LOG_TAG = "RAOPPlayer";
static constexpr uint32_t FRAMES_PER_CHUNK = 352;

std::vector<PcmDevice> RAOPPlayer::pcm_list(const std::string& parameter)
{
    std::ignore = parameter;
    return {};
}

RAOPPlayer::RAOPPlayer(boost::asio::io_context& io_context, const ClientSettings::Player& settings, std::shared_ptr<Stream> stream)
    : Player(io_context, settings, std::move(stream))
{
    auto params = utils::string::split_pairs(settings.parameter, ',', '=');

    auto it = params.find("host");
    if (it == params.end())
        throw SnapException("Please specify a AirPlay host (raop:host=<ip>[,port=<port>])");

    _host = it->second;
    
    try
    {
        auto it = params.find("port");
        _port = it != params.end() ? std::stoi(it->second) : 5000;
    }
    catch (std::exception& ex)
    {
        throw SnapException("Invalid port specified");
    }

    LOG(DEBUG, LOG_TAG) << "Requested RAOP device " << _host << ":" << _port << "\n";
}

RAOPPlayer::~RAOPPlayer()
{
    LOG(DEBUG, LOG_TAG) << "Destructor\n";
    stop(); // NOLINT
}

void RAOPPlayer::setVolume(const Volume& volume)
{
    volume_ = volume;
    _volumeChangeRequested = true;
}

void RAOPPlayer::worker()
{
    // TODO Check that format is 44100:16:2?
  
    auto & format = stream_->getFormat();    
    const auto buffer_size = FRAMES_PER_CHUNK * format.frameSize();
    const auto buffer_duration = std::chrono::milliseconds(TS2MS(FRAMES_PER_CHUNK, format.rate()));
    
    std::vector<uint8_t> buffer(buffer_size);

    LOG(INFO, LOG_TAG) << "Creating RAOP controller\n";

    // ...

    raopcl_s* raopcl = raopcl_create(
      in_addr{ INADDR_ANY },
      0, 0,         // Port base and range
      NULL, NULL,   // DACP id and active remote
      RAOP_PCM,     // Codec
      FRAMES_PER_CHUNK,
      MS2TS(250, 44100), // Latency 
      raop_crypto_t::RAOP_CLEAR, 
      false, "", "", 
      "4", "",      // et & md 
      format.rate(), format.bits(), format.channels(),   // Audio format
      raopcl_float_volume(/*volume_.volume * 100*/5) // TODO get current volume
    );

    if (!raopcl)
        throw SnapException("Cannot init RAOP");

    LOG(INFO, LOG_TAG) << "Connecting to device\n";

    // get player's address
    auto hostent = gethostbyname(_host.c_str());
    if (!hostent)
        throw SnapException("Cannot resolve name " + _host);

    in_addr addr;
    memcpy(&addr.s_addr, hostent->h_addr_list[0], hostent->h_length);

    // connect to player
    if (!raopcl_connect(raopcl, addr, _port, true))
        throw SnapException("Cannot connect to AirPlay device " + _host + ":" + std::to_string(_port) + ", check firewall & port");

    LOG(INFO, LOG_TAG) << "Connected, start sending audio\n";

    // Get the latency in ms, as raop_latency() is expressed in frames
    const auto latency = std::chrono::milliseconds(TS2MS(raopcl_latency(raopcl), raopcl_sample_rate(raopcl)));
    LOG(INFO, LOG_TAG) << "Using latency = " << latency.count() << "ms \n ";

    bool paused = false;
    uint64_t playtime;
    while (active_)
    {
        if (paused)
        {
            if (stream_->waitForChunk(10ms))
            {
                LOG(INFO, LOG_TAG) << "Resuming streaming " << "\n ";
                paused = false;
            }
        }
        else if (raopcl_accept_frames(raopcl))
        {
            if (stream_->getPlayerChunkOrSilence(buffer.data(), latency, FRAMES_PER_CHUNK))
            {
                LOG(DEBUG, LOG_TAG) << "Sending chunk of size " << buffer.size() << "\n";
                raopcl_send_chunk(raopcl, buffer.data(), FRAMES_PER_CHUNK, &playtime);
            }
            else 
            {
                LOG(INFO, LOG_TAG) << "Pausing streaming " << "\n ";
                raopcl_pause(raopcl);
                raopcl_flush(raopcl);
                paused = true;
            }
        }
        else
        {
            LOG(DEBUG, LOG_TAG) << "Device does not accept frame right now" << "\n";
            std::this_thread::sleep_for(10ms);
        }

        if (_volumeChangeRequested)
        {
            int newVol = volume_.mute ? 0 : volume_.volume * 100;
            LOG(INFO, LOG_TAG) << "Changing device volume to " << std::to_string(newVol) << "\n";
            raopcl_set_volume(raopcl, raopcl_float_volume(newVol));

            _volumeChangeRequested = false;
        }
    }

    LOG(INFO, LOG_TAG) << "Closing connection to device\n";
    raopcl_destroy(raopcl);
}

} // namespace player
