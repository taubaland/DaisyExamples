#pragma once
#ifndef BOONTA_SYSEX_H
#define BOONTA_SYSEX_H

#include <stddef.h>
#include <stdint.h>

#include "Effect.h"

/** The pedal describing itself.
 *
 *  Everything else the editor knows about this pedal is a transcription: the
 *  web app's `js/midi.js` restates `MidiMap.h` and its `js/params.js` restates
 *  the parameter ranges, and if either drifts the editor sends the wrong
 *  numbers with nothing able to notice. That was tolerable while there were
 *  three effects and they were always the same three.
 *
 *  It stopped being tolerable when slots became pluggable. The registry is now
 *  build-dependent -- `ID_RNBO` exists only in builds with a patch compiled in
 *  -- so there is no longer a static answer to "what effects does this pedal
 *  have" for anyone to transcribe. The firmware has to say.
 *
 *  It already knows how. `EffectDesc` carries the name, the LED colour, what
 *  the slot's toggle does and the six parameter names; that is exactly the
 *  catalogue an editor needs to draw itself. This encodes it.
 *
 *  ## Frame
 *
 *      F0 7D 42 <cmd> <payload...> F7
 *
 *  `0x7D` is the manufacturer id the MIDI specification reserves for
 *  non-commercial use, which is what this is -- squatting on a real
 *  manufacturer's id would be rude and could collide. `0x42` narrows it
 *  further so the pedal ignores other people's educational-id traffic rather
 *  than trying to parse it.
 *
 *  Every byte between F0 and F7 must have bit 7 clear; that is the whole
 *  constraint that shapes what follows. Colours are 0-127 rather than floats,
 *  strings are length-prefixed rather than NUL-terminated (a length is one
 *  byte and cannot be forgotten), and anything non-ASCII in a name becomes
 *  '?'.
 *
 *  ## Messages
 *
 *      -> F0 7D 42 01 F7                          ask what this is
 *      <- F0 7D 42 41 <proto> <effects> <slots>
 *                     <knobs> <presets> F7
 *
 *      -> F0 7D 42 02 F7                          ask for the catalogue
 *      <- F0 7D 42 42 <id> <r> <g> <b>            one per effect, in id order
 *                     <str name> <str toggle>
 *                     <str param0> .. <str param5> F7
 *
 *  where `<str>` is a length byte followed by that many 7-bit characters.
 *
 *  One message per effect rather than one for the lot: each stays a couple of
 *  hundred bytes whatever the registry grows to, a reader can build its UI
 *  incrementally, and a single dropped message costs one effect rather than
 *  the whole catalogue. The identity reply carries the count, so a reader
 *  knows how many to expect.
 */
namespace boontasysex
{
static constexpr uint8_t kSysexStart = 0xF0;
static constexpr uint8_t kSysexEnd   = 0xF7;

/** Reserved by the MIDI specification for non-commercial use. */
static constexpr uint8_t kManufacturer = 0x7D;

/** Ours within that id, so other 7D traffic is ignored rather than parsed. */
static constexpr uint8_t kDevice = 0x42;

/** Bumped when the shape of a message changes. A reader that does not
 *  recognise the version should say so rather than guess. */
static constexpr uint8_t kProtocolVersion = 1;

enum Command : uint8_t
{
    kReqIdentity  = 0x01,
    kReqCatalogue = 0x02,

    kRspIdentity = 0x41,
    kRspEffect   = 0x42,
};

/** What a received message is asking for. */
enum class Request
{
    NONE, /**< not ours, malformed, or a command we do not have */
    IDENTITY,
    CATALOGUE,
};

/** Longest message this can produce: the frame, the fixed fields, and eight
 *  strings each of which is a length byte plus at most 127 characters. Well
 *  above anything a real descriptor produces, and the encoders clamp to it. */
static constexpr size_t kMaxMessage = 8 + 8 * (1 + 127);

/** Decode a received message.
 *
 *  Takes the bytes *between* F0 and F7, which is what libDaisy's parser hands
 *  over -- it strips the start byte and may or may not include the terminator
 *  depending on the transport, so both are tolerated.
 */
Request ParseRequest(const uint8_t* data, size_t length);

/** Build the identity reply. Returns the number of bytes written. */
size_t EncodeIdentity(uint8_t* out,
                      size_t   capacity,
                      int      effect_count,
                      int      slot_count,
                      int      knob_count,
                      int      preset_count);

/** Build one effect's catalogue entry. Returns bytes written, or 0 if the
 *  descriptor will not fit in `capacity`. */
size_t EncodeEffect(uint8_t*          out,
                    size_t            capacity,
                    int               id,
                    const EffectDesc& desc);

} // namespace boontasysex

#endif
