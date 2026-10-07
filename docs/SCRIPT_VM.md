# Script interpreter

Evidence labels as in `ENGINE_NOTES.md`: **[S]** read from the disassembly,
**[D]** observed under the tracer, **[I]** inferred.

## Execution model [S]

- A5 is the script pointer, A6 the end of the current block.
- Every instruction starts with a 16-bit opcode. The handler table is at
  0x2354 (34 entries).
- There are two run loops:
  - **scan** (0x23DC) calls handlers with a zero flag. Handlers that are
    *block headers* test whether their block applies. A header that applies
    sets bit 0 of 0xFF0AC9 and leaves A5 on itself, which ends the loop.
    Every other opcode just skips itself.
  - **exec** (0x2406) calls handlers with a non-zero flag. Action opcodes do
    their work. Block headers skip their whole block.
- 0xFF0878 holds the address of the loop in use so nested blocks run in the
  same mode.
- Exec stops early when bit 1 and bit 0 of 0xFF0ACA are both set (skip
  requested with Start during a skippable sequence). [S, meaning I]

0xFF0AC9 bits: 0 match found, 1 verb headers enabled, 2 dialogue-choice
headers enabled, 4 tick headers enabled, 5 waiting for a second object,
7 walk-on trigger already fired.

## Conditions [S]

Used by opcodes 0x03 and 0x04. Fields at +6, +8, +0xA, +0xC and a flag word
at +0xE.

| Flag bit | Meaning |
|---|---|
| 0 | Left side is story flag: byte +6 of 0xFF2A00, bit +8 |
| 1 | Left side is an object field: offset +6 in object +8 minus 3. Object 1 with offset 6 means the current room |
| neither | Left side is the immediate at +6 |
| 2 | Right side is a story flag: byte +0xA, bit +0xC |
| 3 | Right side is an object field: offset +0xA, object +0xC minus 3 |
| neither | Right side is the immediate at +0xA |
| 4 | Test equal |
| 5 | Test left greater than right, signed |
| 6 | Test left less than right, signed |
| none of 4-6 | Test not equal |

## Opcodes decoded so far [S]

Sizes are in bytes. "who" is 1 for the first lead character, 2 for the
second, and 3 or more for object number who minus 3.

| Op | Size | Mode | Meaning |
|---|---|---|---|
| 0x00 | 2 | both | No operation |
| 0x01 | block | scan | Verb header: word verb, word second object, word block length. Matches the current verb. Verbs 5 and 6 take a second object and otherwise put the interface into "pick the second object" state |
| 0x02 | block | scan | Dialogue choice: two text offsets, block length, a word. Appends to a list of at most five choices |
| 0x03 | 16 | both | If: enter the block when the condition holds, else jump by the word at +2 |
| 0x04 | 16 | both | If with nested run: runs the block through the active loop, then skips the else part |
| 0x05 | 6 | exec | Move object to a room. Handles characters entering or leaving the current room and the inventory list. High bit of the object word suppresses redraw |
| 0x06 | 6 | exec | Play animation on who, wait until it is taken up |
| 0x07 | 4 | exec | Mark object for redraw, or refresh the inventory page |
| 0x08 | 12 | exec | Assign: destination is a story flag bit or an object field. Source is a flag bit, an object field, an immediate, or a random number below the immediate. Operations set, subtract, or, add |
| 0x09 | 6 | exec | Set one byte of an object (verb defaults at +0x16) |
| 0x0A | 6 | exec | Set object word +4, refresh inventory if carried |
| 0x0B | 6 | exec | Set object picture state, optionally waiting for the redraw |
| 0x0C | 4 | exec | Call native function by index (table at 0x2EB4, 62 entries) |
| 0x13 | block | scan | Tick header: matches when the per-frame object pass is scanning |
| 0x14 | 6 | exec | Set object word +2 (hotspot index) |
| 0x16 | 4 | exec | Hide the actor for who |
| 0x19 | 4 | exec | Show the actor for who, waiting for its first frame |
| 0x1A | 10 | exec | Start or stop a palette cycle in one of five slots |
| 0x1C | 4 | exec | Copy "actor is walking" into story flag byte 0 bit 2 |
| 0x1D | 4 | exec | Copy "actor animation finished" into story flag byte 0 bit 3 |
| 0x1E | 6 | exec | Turn a lead character to a facing, optionally waiting |

Opcodes 0x0D-0x12, 0x15, 0x17, 0x18, 0x1B, 0x1F-0x21 are still to be read.

## Object table [S]

Built at 0xFF1200 by the episode loader (0x7F66), 26 bytes per object.

| Offset | Size | Meaning |
|---|---|---|
| 0x00 | word | Picture or animation state. High bits flag a pending erase |
| 0x02 | word | Hotspot rectangle index, 1-based, 0 for none. For characters, the graphics set id |
| 0x04 | word | Name or inventory icon reference [I] |
| 0x06 | word | Room number. 1 means carried |
| 0x08 | word | |
| 0x12 | word | Character start X |
| 0x14 | word | Character start Y |
| 0x16 | byte | Default verb |
| 0x18 | byte | Flags: bit 0 redraw, bit 1 is a character, bit 2 tick script armed, bit 3 scalable |
| 0x19 | byte | Actor slot minus 2 when present in the room, else 0xFF |

The carried-object list is at 0xFF0A1E: object indices ended by 0xFFFF.
0xFF06EC is the inventory page (four items per page).

## Episode header [S]

Pointer table at 0x31AF2, one entry per episode.

| Offset | Meaning |
|---|---|
| 0x00, 0x04 | Start and end of the default story flags, and start of object definitions |
| 0x08, 0x0C | Start and end of the room table, 20 bytes per room |
| 0x1C, 0x24 | Hotspot rectangle data base and offset table |
| 0x20 | Text base |
| 0x28 | Object script table, 8 bytes per object |
| 0x2C | Script base |
| 0x30 | Start record: start room, music, length, then the init script |

## Passwords [S]

A password stores the room number, a checksum and the first 29 story flag
bytes. The 31 bytes are XOR-chained, cut into 50 five-bit symbols and shown
as two lines of five groups of five characters from a 32-symbol alphabet.
