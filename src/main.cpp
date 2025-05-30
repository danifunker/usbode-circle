//
// main.c
//
// Circle - A C++ bare metal environment for Raspberry Pi
// Copyright (C) 2014  R. Stange <rsta2@o2online.de>
//
// This program is free software: you can redistribute it and/or modify
// it under the terms of the GNU General Public License as published by
// the Free Software Foundation, either version 3 of the License, or
// (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program.  If not, see <http://www.gnu.org/licenses/>.
//
#include <circle/startup.h>

#include "kernel.h"

int main(void) {
    // cannot return here because some destructors used in CKernel are not implemented

    CKernel Kernel;
    if (!Kernel.Initialize()) {
        halt();
        return EXIT_HALT;
    }

    TShutdownMode ShutdownMode = Kernel.Run();

    switch (ShutdownMode) {
        case ShutdownReboot:
            reboot();
            return EXIT_REBOOT;

        case ShutdownHalt:
        default:
            halt();
            return EXIT_HALT;
    }
}
