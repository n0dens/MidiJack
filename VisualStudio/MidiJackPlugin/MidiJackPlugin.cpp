#include "stdafx.h"
#include <string>

namespace
{
    // Basic type aliases
    using DeviceInHandle = HMIDIIN;
    using DeviceID = uint32_t;
    using DeviceOutHandle = HMIDIOUT;

    // Utility functions for Win32/64 compatibility
#ifdef _WIN64
    DeviceID DeviceHandleToID(DeviceInHandle handle)
    {
        return static_cast<DeviceID>(reinterpret_cast<uint64_t>(handle));
    }
    DeviceInHandle DeviceIDToHandle(DeviceID id)
    {
        return reinterpret_cast<DeviceInHandle>(static_cast<uint64_t>(id));
    }
#else
    DeviceID DeviceHandleToID(DeviceInHandle handle)
    {
        return reinterpret_cast<DeviceID>(handle);
    }
    DeviceHandle DeviceIDToHandle(DeviceID id)
    {
        return reinterpret_cast<DeviceInHandle>(id);
    }
#endif

    // MIDI message storage class
    class MidiMessage
    {
        DeviceID source_;
        uint8_t status_;
        uint8_t data1_;
        uint8_t data2_;

    public:

        MidiMessage(DeviceID source, uint32_t rawData)
            : source_(source), status_(rawData), data1_(rawData >> 8), data2_(rawData >> 16)
        {
        }

        uint64_t Encode64Bit()
        {
            uint64_t ul = source_;
            ul |= (uint64_t)status_ << 32;
            ul |= (uint64_t)data1_ << 40;
            ul |= (uint64_t)data2_ << 48;
            return ul;
        }

        std::string ToString()
        {
            char temp[256];
            std::snprintf(temp, sizeof(temp), "(%X) %02X %02X %02X", source_, status_, data1_, data2_);
            return temp;
        }
    };

    // Incoming MIDI message queue
    std::queue<MidiMessage> message_queue;

    // Device handler lists
    std::list<DeviceInHandle> active_in_handles;
    std::vector<DeviceOutHandle> active_out_handles;
    std::stack<DeviceInHandle> in_handles_to_close;
    std::stack<DeviceOutHandle> out_handles_to_close;

    DeviceOutHandle defaultDevice;

    // Mutex for resources
    std::recursive_mutex resource_lock;

    // MIDI input callback
    static void CALLBACK MidiInProc(HMIDIIN hMidiIn, UINT wMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2)
    {
        if (wMsg == MIM_DATA)
        {
            DeviceID id = DeviceHandleToID(hMidiIn);
            uint32_t raw = static_cast<uint32_t>(dwParam1);
            resource_lock.lock();
            message_queue.push(MidiMessage(id, raw));
            resource_lock.unlock();
        }
        else if (wMsg == MIM_CLOSE)
        {
            resource_lock.lock();
            in_handles_to_close.push(hMidiIn);
            resource_lock.unlock();
        }
    }

    static void CALLBACK MidiOutProc(HMIDIOUT hMidiOut, UINT wMsg, DWORD_PTR dwInstance, DWORD_PTR dwParam1, DWORD_PTR dwParam2)
    {
        if (wMsg == MOM_CLOSE)
        {
            resource_lock.lock();
            out_handles_to_close.push(hMidiOut);
            resource_lock.unlock();
        }
    }

    // Retrieve a name of a given In device.
    std::string GetInDeviceName(DeviceInHandle handle)
    {
        auto casted_id = reinterpret_cast<UINT_PTR>(handle);
        MIDIINCAPS caps;
        if (midiInGetDevCaps(casted_id, &caps, sizeof(caps)) == MMSYSERR_NOERROR) {
            std::wstring name(caps.szPname);
            return std::string(name.begin(), name.end());
        }
        return "unknown";
    }

    // Retrieve a name of a given Out Device
    std::string GetOutDeviceName(DeviceOutHandle handle)
    {
        auto casted_id = reinterpret_cast<UINT_PTR>(handle);
        MIDIOUTCAPS caps;
        if (midiOutGetDevCaps(casted_id, &caps, sizeof(caps)) == MMSYSERR_NOERROR)
        {
            std::wstring name(caps.szPname);
            return std::string(name.begin(), name.end());
        }
        return "unknown";
    }

    // Open a MIDI device with a given index.
    void OpenInDevice(unsigned int index)
    {
        static const DWORD_PTR callback = reinterpret_cast<DWORD_PTR>(MidiInProc);
        DeviceInHandle handle;
        if (midiInOpen(&handle, index, callback, NULL, CALLBACK_FUNCTION) == MMSYSERR_NOERROR)
        {
            if (midiInStart(handle) == MMSYSERR_NOERROR)
            {
                resource_lock.lock();
                active_in_handles.push_back(handle);
                resource_lock.unlock();
            }
            else
            {
                midiInClose(handle);
            }
        }
    }

    void ClearEmptyOutHandles() {
        active_out_handles.erase(std::remove(begin(active_out_handles), end(active_out_handles), nullptr), end(active_out_handles));
    }

    // Open a MIDI device on a given index to receive outbound messages
    void OpenOutDevice(unsigned int index) {
        ClearEmptyOutHandles();
        static const DWORD_PTR callback = reinterpret_cast<DWORD_PTR>(MidiOutProc);

        DeviceOutHandle handle;
        if (midiOutOpen(&handle, index, callback, NULL, CALLBACK_FUNCTION) == MMSYSERR_NOERROR)
        {
            resource_lock.lock();
            if (std::find(active_out_handles.begin(), active_out_handles.end(), handle) == active_out_handles.end())
            {
                active_out_handles.push_back(handle);
            }
            resource_lock.unlock();
        }
    }

    // Close a given handler.
    void CloseInDevice(DeviceInHandle handle)
    {
        midiInClose(handle);

        resource_lock.lock();
        active_in_handles.remove(handle);
        resource_lock.unlock();
    }

    // Reset a given out device and close the port to it
    void CloseOutDevice(DeviceOutHandle handle)
    {
        midiOutReset(handle);
        midiOutClose(handle);

        resource_lock.lock();
        active_out_handles.erase(std::remove(active_out_handles.begin(), active_out_handles.end(), handle), active_out_handles.end());
        resource_lock.unlock();
    }

    // Open the MIDI in devices.
    void OpenAllInDevices()
    {
        int device_count = midiInGetNumDevs();
        for (int i = 0; i < device_count; i++) OpenInDevice(i);
    }

    // Open all out devices
    void OpenAllOutDevices()
    {
        int device_count = midiOutGetNumDevs();
        for (int i = 0; i < device_count; i++) OpenOutDevice(i);
    }

    // Refresh device handlers
    void RefreshDevices()
    {
        resource_lock.lock();

        // Close disconnected handlers.
        while (!in_handles_to_close.empty()) {
            CloseInDevice(in_handles_to_close.top());
            in_handles_to_close.pop();
        }

        while (!out_handles_to_close.empty()) {
            CloseOutDevice(out_handles_to_close.top());
            out_handles_to_close.pop();
        }

        // Try open all devices to detect newly connected ones.
        OpenAllInDevices();
        OpenAllOutDevices();
        resource_lock.unlock();
    }

    // Close the all devices.
    void CloseAllDevices()
    {
        resource_lock.lock();
        while (!active_in_handles.empty())
            CloseInDevice(active_in_handles.front());

        for (DeviceOutHandle &handle : active_out_handles)
        {
            CloseOutDevice(handle);
        }
        resource_lock.unlock();
    }

    // Return the name of the device at the specified index
    std::string GetOutDeviceAtIndex(int index)
    {
        std::string deviceName;

        if (index < active_out_handles.size() && index >= 0)
        {
            deviceName = GetOutDeviceName(active_out_handles[index]);
        }
        else
        {
            deviceName = "NO_DEVICE_AT_INDEX";
        }

        return deviceName;
    }

    // Send a message to the specified out device
    std::string SendToDevice(DeviceOutHandle device, uint8_t aStatus, uint8_t aData1, uint8_t aData2) {
        // Pack MIDI bytes into double word.
        DWORD packet;
        unsigned char *ptr = (unsigned char *)&packet;
        ptr[0] = aStatus;
        ptr[1] = aData1;
        ptr[2] = aData2;
        ptr[3] = 0;

        return std::to_string(midiOutShortMsg(device, packet));
    }
}

/* Exported functions */

#define EXPORT_API extern "C" __declspec(dllexport)

// Counts the number of endpoints.
EXPORT_API int MidiJackCountEndpoints()
{
    return static_cast<int>(active_in_handles.size());
}

// Get the unique ID of an endpoint.
EXPORT_API uint32_t MidiJackGetEndpointIDAtIndex(int index)
{
    auto itr = active_in_handles.begin();
    std::advance(itr, index);
    return DeviceHandleToID(*itr);
}

// Get the name of an endpoint.
EXPORT_API const char* MidiJackGetEndpointName(uint32_t id)
{
    auto handle = DeviceIDToHandle(id);
    static std::string buffer;
    buffer = GetInDeviceName(handle);
    return buffer.c_str();
}

// Retrieve and erase an MIDI message data from the message queue.
EXPORT_API uint64_t MidiJackDequeueIncomingData()
{
    RefreshDevices();

    if (message_queue.empty()) return 0;

    resource_lock.lock();
    auto msg = message_queue.front();
    message_queue.pop();
    resource_lock.unlock();

    return msg.Encode64Bit();
}

// Open ports for all MIDI out devices and return a list of them as a single char array
EXPORT_API const char* MidiJackGetOutDevices() {
    OpenAllOutDevices();

    static std::string names;
    names.clear();

    for (DeviceOutHandle &handle : active_out_handles)
    {
        names.append(GetOutDeviceName(handle) + ",");
    }

    return names.c_str();
}

// Sends a message to the given device and returns the result of the attempt
EXPORT_API const char* MidiJackSendToDevice(int deviceID, uint8_t aStatus, uint8_t aData1, uint8_t aData2) {
    std::string result = GetOutDeviceAtIndex(deviceID);
    if (result != "NO_DEVICE_AT_INDEX")
    {
        SendToDevice(active_out_handles[deviceID], aStatus, aData1, aData2);
    }

    return result.c_str();
}