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


#pragma once


// local headers
#include "player.hpp"

// 3rd party headers
#include <boost/asio/steady_timer.hpp>

// standard headers
#include <cstdio>
#include <memory>

// forward declaration to avoid including header
struct raopcl_s;

namespace player
{

static constexpr auto RAOP = "raop";

class RAOPPlayer : public Player
{
public:
    /// c'tor
    RAOPPlayer(boost::asio::io_context& io_context, const ClientSettings::Player& settings, std::shared_ptr<Stream> stream);
    /// d'tor
    ~RAOPPlayer() override = default;

    /// List the dummy file PCM device
    static std::vector<PcmDevice> pcm_list(const std::string& parameter);

    void setVolume(const Volume& volume) override;
    void start() override;

private:
    //void requestAudio();

    void worker() override;
    bool needsThread() const override { return true; }

    raopcl_s* raopcl = nullptr;
    std::string host_;
    int port_ = 5000;
    std::string et_;
    bool usePCMEncoding_ = false;
    bool volumeChangeRequested_ = false;
};

} // namespace player
