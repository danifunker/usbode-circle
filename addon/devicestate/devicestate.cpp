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
    static DeviceState& getInstance() {
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

