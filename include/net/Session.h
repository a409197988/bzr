/*
 * Bael'Zharon's Respite
 * Copyright (C) 2014 Daniel Skorupski
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along
 * with this program; if not, write to the Free Software Foundation, Inc.,
 * 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
 */
#ifndef BZR_NET_SESSION_H
#define BZR_NET_SESSION_H

#include <chrono>

typedef chrono::high_resolution_clock net_clock;
typedef net_clock::time_point net_time_point;

struct Packet;

class Session
{
public:
    void handle(const Packet& packet);
    void tick(net_time_point now);

    uint32_t remoteIp() const;
    uint16_t remotePort() const;
    bool dead() const;
    net_time_point nextTick() const;
    
private:
    void sendLogon(const string& name, const string& key);
};

#endif