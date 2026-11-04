#include "Sysex.h"

namespace boontasysex
{
namespace
{
    /** 7-bit or nothing. Every byte inside a SysEx frame has to have bit 7 clear,
 *  so anything outside printable ASCII becomes '?' rather than truncating the
 *  message or, worse, emitting a byte a parser would read as a status. */
    inline uint8_t Printable(char c)
    {
        const uint8_t u = static_cast<uint8_t>(c);
        return (u >= 0x20 && u < 0x7F) ? u : static_cast<uint8_t>('?');
    }

    /** A float from 0 to 1 as 0-127. Colours arrive as floats and have to survive
 *  the 7-bit rule; a hundred and twenty-eight steps is far more than an LED
 *  driver or a CSS colour can show apart. */
    inline uint8_t Seven(float v)
    {
        if(!(v > 0.f)) // also catches NaN, whose every comparison is false
            return 0;
        if(v >= 1.f)
            return 127;
        return static_cast<uint8_t>(v * 127.f + 0.5f);
    }

    /** Length-prefixed string. Returns bytes written, or 0 if it will not fit --
 *  including the length byte, so a caller that checks the return never emits a
 *  half-written string. */
    size_t PutString(uint8_t* out, size_t capacity, const char* s)
    {
        if(capacity < 1)
            return 0;

        size_t len = 0;
        if(s)
            while(s[len] != '\0' && len < 127)
                len++;

        if(capacity < len + 1)
            return 0;

        out[0] = static_cast<uint8_t>(len);
        for(size_t i = 0; i < len; i++)
            out[1 + i] = Printable(s[i]);
        return len + 1;
    }

    /** The four bytes every message starts with, after F0. */
    size_t PutHeader(uint8_t* out, size_t capacity, uint8_t command)
    {
        if(capacity < 4)
            return 0;
        out[0] = kSysexStart;
        out[1] = kManufacturer;
        out[2] = kDevice;
        out[3] = command;
        return 4;
    }

} // namespace

Request ParseRequest(const uint8_t* data, size_t length)
{
    if(!data)
        return Request::NONE;

    // Transports disagree about whether the caller sees F0 and F7. Skip a
    // leading start byte and ignore a trailing terminator so the same parser
    // works for both rather than one of them silently never matching.
    if(length > 0 && data[0] == kSysexStart)
    {
        data++;
        length--;
    }
    if(length > 0 && data[length - 1] == kSysexEnd)
        length--;

    if(length < 3)
        return Request::NONE;
    if(data[0] != kManufacturer || data[1] != kDevice)
        return Request::NONE;

    switch(data[2])
    {
        case kReqIdentity: return Request::IDENTITY;
        case kReqCatalogue: return Request::CATALOGUE;
        default: return Request::NONE;
    }
}

size_t EncodeIdentity(uint8_t* out,
                      size_t   capacity,
                      int      effect_count,
                      int      slot_count,
                      int      knob_count,
                      int      preset_count)
{
    if(!out)
        return 0;

    size_t n = PutHeader(out, capacity, kRspIdentity);
    if(n == 0)
        return 0;

    const int counts[] = {effect_count, slot_count, knob_count, preset_count};

    if(capacity < n + 1 + sizeof(counts) / sizeof(counts[0]) + 1)
        return 0;

    out[n++] = kProtocolVersion;
    for(int c : counts)
        out[n++] = static_cast<uint8_t>(c < 0 ? 0 : (c > 127 ? 127 : c));

    out[n++] = kSysexEnd;
    return n;
}

size_t
EncodeEffect(uint8_t* out, size_t capacity, int id, const EffectDesc& desc)
{
    if(!out)
        return 0;

    size_t n = PutHeader(out, capacity, kRspEffect);
    if(n == 0)
        return 0;

    if(capacity < n + 4)
        return 0;

    out[n++] = static_cast<uint8_t>(id < 0 ? 0 : (id > 127 ? 127 : id));
    out[n++] = Seven(desc.r);
    out[n++] = Seven(desc.g);
    out[n++] = Seven(desc.b);

    // The name, what the toggle does, then the six parameters in knob order --
    // which is the order the DSP addresses them in, so a reader can label its
    // knobs left to right without a lookup.
    const char* strings[2 + Effect::kParamCount];
    strings[0] = desc.name;
    strings[1] = desc.toggle;
    for(int i = 0; i < Effect::kParamCount; i++)
        strings[2 + i] = desc.param[i].name;

    for(const char* s : strings)
    {
        // Leave room for the terminator, or a descriptor that exactly fills
        // the buffer would produce a message with no F7 on the end. Checked
        // before subtracting, because these are size_t and `capacity - n - 1`
        // on a full buffer would wrap to something enormous rather than
        // failing.
        if(capacity < n + 2)
            return 0;
        const size_t written = PutString(out + n, capacity - n - 1, s);
        if(written == 0)
            return 0;
        n += written;
    }

    out[n++] = kSysexEnd;
    return n;
}

} // namespace boontasysex
