//
// A device state singleton. Currently used to communicate between parts
// of USBODE and the main kernel loop to indicate reboot or shutdown
//
// This is the entry point for listing and mounting CD images. All parts
// of USBODE will use this, not just the SCSI Toolbox
//
//
// Copyright (C) 2025 Ian Cass
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
#ifndef DEVICE_STATE_H
#define DEVICE_STATE_H

#ifndef TSHUTDOWNMODE
#define TSHUTDOWNMODE
enum TShutdownMode {
    ShutdownNone,
    ShutdownHalt,
    ShutdownReboot
};
#endif

class DeviceState {
public:
    static DeviceState& Get() {
        static DeviceState instance;
        return instance;
    }

    TShutdownMode getShutdownMode() const {
        return shutdownMode;
    }

    void setShutdownMode(TShutdownMode state) {
        shutdownMode = state;
    }

    DeviceState(const DeviceState&) = delete;
    DeviceState& operator=(const DeviceState&) = delete;
    DeviceState(DeviceState&&) = delete;
    DeviceState& operator=(DeviceState&&) = delete;

private:
    DeviceState() : shutdownMode(ShutdownNone) {}
    ~DeviceState() = default;

    TShutdownMode shutdownMode;
};

#endif // DEVICE_STATE_H

