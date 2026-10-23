// Compile real production PIN policy and screen-fit constants. No hardware test.
#include "core/vault_format.hpp"
#include "core/vault_interaction.hpp"
static_assert(vault::validPin("1234",4));
static_assert(vault::validPin("0000",4));
static_assert(vault::validPin("012345678901",12));
static_assert(!vault::validPin("123",3));
static_assert(!vault::validPin("1234567890123",13));
static_assert(!vault::validPin("123x",4));
static_assert(!vault::validPin("12 4",4));
static_assert(!vault::validPin("12\0x",4));
static_assert(!vault::validPin(nullptr,4));
static_assert(vault::Rounds==0 && vault::PinRounds==10000);
static_assert(vault::HeaderSize==128 && vault::MaxEntries==256);
static_assert(vault::MaxNoteBytes==256);
static_assert(36+1+203==240);
static_assert(vault::interaction::PinLayout::ContentHeight <=
              vault::interaction::PinLayout::BodyHeight); // real PIN layout
static_assert(36+4*40+4<=203);  // toolbar and four categories
static_assert(3*79+2<=240);     // numeric touch width
