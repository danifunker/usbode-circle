//
// Host-build stub for <circle/usb/usb.h>.
// Only the descriptor structures and constants USBODE's gadget code
// references. Layouts match Circle's originals byte for byte.
//
#ifndef _circle_usb_usb_h
#define _circle_usb_usb_h

#include <circle/macros.h>
#include <circle/types.h>

#define DESCRIPTOR_DEVICE 1
#define DESCRIPTOR_CONFIGURATION 2
#define DESCRIPTOR_STRING 3
#define DESCRIPTOR_INTERFACE 4
#define DESCRIPTOR_ENDPOINT 5

struct TSetupData
{
    u8 bmRequestType;
    u8 bRequest;
    u16 wValue;
    u16 wIndex;
    u16 wLength;
} PACKED;

struct TUSBDeviceDescriptor
{
    u8 bLength;
    u8 bDescriptorType;
    u16 bcdUSB;
    u8 bDeviceClass;
    u8 bDeviceSubClass;
    u8 bDeviceProtocol;
    u8 bMaxPacketSize0;
    u16 idVendor;
    u16 idProduct;
    u16 bcdDevice;
    u8 iManufacturer;
    u8 iProduct;
    u8 iSerialNumber;
    u8 bNumConfigurations;
} PACKED;

struct TUSBConfigurationDescriptor
{
    u8 bLength;
    u8 bDescriptorType;
    u16 wTotalLength;
    u8 bNumInterfaces;
    u8 bConfigurationValue;
    u8 iConfiguration;
    u8 bmAttributes;
    u8 bMaxPower;
} PACKED;

struct TUSBInterfaceDescriptor
{
    u8 bLength;
    u8 bDescriptorType;
    u8 bInterfaceNumber;
    u8 bAlternateSetting;
    u8 bNumEndpoints;
    u8 bInterfaceClass;
    u8 bInterfaceSubClass;
    u8 bInterfaceProtocol;
    u8 iInterface;
} PACKED;

struct TUSBEndpointDescriptor
{
    u8 bLength;
    u8 bDescriptorType;
    u8 bEndpointAddress;
    u8 bmAttributes;
    u16 wMaxPacketSize;
    u8 bInterval;
} PACKED;

#endif
