//
// ftpdaemon.h
//
// mt32-pi - A baremetal MIDI synthesizer for Raspberry Pi
// Copyright (C) 2020-2023 Dale Whinham <daleyo@gmail.com>
//
// This file is part of mt32-pi.
//
// mt32-pi is free software: you can redistribute it and/or modify it under the
// terms of the GNU General Public License as published by the Free Software
// Foundation, either version 3 of the License, or (at your option) any later
// version.
//
// mt32-pi is distributed in the hope that it will be useful, but WITHOUT ANY
// WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS
// FOR A PARTICULAR PURPOSE. See the GNU General Public License for more
// details.
//
// You should have received a copy of the GNU General Public License along with
// mt32-pi. If not, see <http://www.gnu.org/licenses/>.
//

#ifndef _ftpdaemon_h
#define _ftpdaemon_h

#include <circle/net/socket.h>
#include <circle/sched/task.h>

class CFTPDaemon : protected CTask {
   public:
    CFTPDaemon(const char* pUser, const char* pPassword);
    virtual ~CFTPDaemon() override;

    bool Initialize();

    virtual void Run() override;

   private:
    // TCP sockets
    CSocket* m_pListenSocket;

    const char* m_pUser;
    const char* m_pPassword;
};

#endif
