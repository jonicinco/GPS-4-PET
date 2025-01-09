/*

Credentials file

*/

#pragma once

// Only one of these settings must be defined
#define USE_ABP
//#define USE_OTAA

#ifdef USE_ABP

    // LoRaWAN NwkSKey, network session key
    static const u1_t PROGMEM NWKSKEY[16] = { 0x3E, 0x66, 0x07, 0x3F, 0x96, 0x73, 0x6B, 0xAE, 0x89, 0x3D, 0x7D, 0x9E, 0x5E, 0x99, 0x9D, 0xAE };
    // LoRaWAN AppSKey, application session key
    static const u1_t PROGMEM APPSKEY[16] = { 0x91, 0x4F, 0xA0, 0xF2, 0x7F, 0x3F, 0xBF, 0x75, 0xC6, 0x19, 0x4E, 0xEE, 0x9A, 0x12, 0x82, 0x87 };
    // LoRaWAN end-device address (DevAddr)
    // This has to be unique for every node
    static const u4_t DEVADDR = 0x260B9B87;

#endif

#ifdef USE_OTAA

    // This EUI must be in little-endian format, so least-significant-byte (lsb)
    // first. When copying an EUI from ttnctl output, this means to reverse
    // the bytes. For TTN issued EUIs the last bytes should be 0x00, 0x00,
    // 0x00.
    static const u1_t PROGMEM APPEUI[8]  = { 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 };

    // This should also be in little endian format (lsb), see above.
    // Note: You do not need to set this field, if unset it will be generated automatically based on the device macaddr
    static u1_t DEVEUI[8]  = { 0xF0, 0xCE, 0x06, 0xD0, 0x7E, 0xD5, 0xB3, 0x70 };

    // This key should be in big endian format (msb) (or, since it is not really a
    // number but a block of memory, endianness does not really apply). In
    // practice, a key taken from ttnctl can be copied as-is.
    // The key shown here is the semtech default key.
    static const u1_t PROGMEM APPKEY[16] = { 0x3C, 0x8C, 0x79, 0x1A, 0x0D, 0x2A, 0xE7, 0xEC, 0x75, 0x1F, 0xBB, 0xE8, 0x2B, 0xD8, 0x39, 0xDD };

#endif
