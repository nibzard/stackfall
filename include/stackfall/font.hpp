#pragma once

// The source-defined 5x7 pixel glyph table of specification section 6.2.
//
// Every UI character is drawn from this table as filled rectangles, so the
// game ships no font file, no sprite atlas, and no texture. One Glyph holds
// the seven rows of one character; each row is five bits, one per column,
// where bit 4 (0b10000) is the leftmost pixel of the row. A character outside
// the table range falls back to the space glyph, so a stray byte in a format
// string can never read out of bounds.
//
// The table covers every printable ASCII character from space (32) through
// tilde (126) in code order, so `glyphFor(c) == kGlyphTable[c - 32]` and the
// headless suite can check the whole table without any SDL type: this header
// includes none.
//
// Rows of every glyph were reviewed as pixel art when the table was written;
// tests/test_font.cpp pins the coverage, the bit range, the distinctness of
// the digits and letters, and the exact shape of several anchor glyphs.

#include <array>
#include <cstdint>

namespace stackfall {

// Glyph cell metrics in font pixels. One blank column separates two glyphs,
// so a glyph advance is six pixels and text keeps a steady rhythm.
inline constexpr int kGlyphColumns = 5;
inline constexpr int kGlyphRows = 7;
inline constexpr int kGlyphSpacing = 1;
inline constexpr int kGlyphAdvance = kGlyphColumns + kGlyphSpacing;  // 6

// The table range: printable ASCII, in code order.
inline constexpr char kFirstGlyphChar = ' ';
inline constexpr char kLastGlyphChar = '~';
inline constexpr int kGlyphCount =
    kLastGlyphChar - kFirstGlyphChar + 1;  // 95 characters

// One character: seven rows of five bits, row 0 at the top.
using Glyph = std::array<std::uint8_t, kGlyphRows>;

inline constexpr std::array<Glyph, kGlyphCount> kGlyphTable = {{
    {0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000},  // 32 ' '
    {0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00000, 0b00100},  // 33 '!'
    {0b01010, 0b01010, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000},  // 34 '"'
    {0b00000, 0b01010, 0b11111, 0b01010, 0b11111, 0b01010, 0b00000},  // 35 '#'
    {0b00100, 0b01111, 0b10000, 0b01110, 0b00001, 0b11110, 0b00100},  // 36 '$'
    {0b11001, 0b11001, 0b00010, 0b00100, 0b01000, 0b10011, 0b10011},  // 37 '%'
    {0b01100, 0b10010, 0b10100, 0b01000, 0b10101, 0b10010, 0b01101},  // 38 '&'
    {0b00100, 0b00100, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000},  // 39 '''
    {0b00110, 0b01000, 0b10000, 0b10000, 0b10000, 0b01000, 0b00110},  // 40 '('
    {0b01100, 0b00010, 0b00001, 0b00001, 0b00001, 0b00010, 0b01100},  // 41 ')'
    {0b00000, 0b00100, 0b10101, 0b01110, 0b10101, 0b00100, 0b00000},  // 42 '*'
    {0b00000, 0b00100, 0b00100, 0b11111, 0b00100, 0b00100, 0b00000},  // 43 '+'
    {0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00100, 0b01000},  // 44 ','
    {0b00000, 0b00000, 0b00000, 0b11111, 0b00000, 0b00000, 0b00000},  // 45 '-'
    {0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b01100, 0b01100},  // 46 '.'
    {0b00001, 0b00001, 0b00010, 0b00100, 0b01000, 0b10000, 0b10000},  // 47 '/'
    {0b01110, 0b10001, 0b10011, 0b10101, 0b11001, 0b10001, 0b01110},  // 48 0
    {0b00100, 0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110},  // 49 1
    {0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b01000, 0b11111},  // 50 2
    {0b11111, 0b00010, 0b00100, 0b00010, 0b00001, 0b10001, 0b01110},  // 51 3
    {0b00010, 0b00110, 0b01010, 0b10010, 0b11111, 0b00010, 0b00010},  // 52 4
    {0b11111, 0b10000, 0b11110, 0b00001, 0b00001, 0b10001, 0b01110},  // 53 5
    {0b00110, 0b01000, 0b10000, 0b11110, 0b10001, 0b10001, 0b01110},  // 54 6
    {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b01000, 0b01000},  // 55 7
    {0b01110, 0b10001, 0b10001, 0b01110, 0b10001, 0b10001, 0b01110},  // 56 8
    {0b01110, 0b10001, 0b10001, 0b01111, 0b00001, 0b00010, 0b01100},  // 57 9
    {0b00000, 0b01100, 0b01100, 0b00000, 0b01100, 0b01100, 0b00000},  // 58 ':'
    {0b00000, 0b01100, 0b01100, 0b00000, 0b01100, 0b01000, 0b10000},  // 59 ';'
    {0b00010, 0b00100, 0b01000, 0b10000, 0b01000, 0b00100, 0b00010},  // 60 '<'
    {0b00000, 0b00000, 0b11111, 0b00000, 0b11111, 0b00000, 0b00000},  // 61 '='
    {0b01000, 0b00100, 0b00010, 0b00001, 0b00010, 0b00100, 0b01000},  // 62 '>'
    {0b01110, 0b10001, 0b00001, 0b00010, 0b00100, 0b00000, 0b00100},  // 63 '?'
    {0b01110, 0b10001, 0b10001, 0b10111, 0b10101, 0b10001, 0b01110},  // 64 '@'
    {0b01110, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001},  // 65 A
    {0b11110, 0b10001, 0b10001, 0b11110, 0b10001, 0b10001, 0b11110},  // 66 B
    {0b01110, 0b10001, 0b10000, 0b10000, 0b10000, 0b10001, 0b01110},  // 67 C
    {0b11100, 0b10010, 0b10001, 0b10001, 0b10001, 0b10010, 0b11100},  // 68 D
    {0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b11111},  // 69 E
    {0b11111, 0b10000, 0b10000, 0b11110, 0b10000, 0b10000, 0b10000},  // 70 F
    {0b01110, 0b10001, 0b10000, 0b10111, 0b10001, 0b10001, 0b01111},  // 71 G
    {0b10001, 0b10001, 0b10001, 0b11111, 0b10001, 0b10001, 0b10001},  // 72 H
    {0b01110, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110},  // 73 I
    {0b00111, 0b00010, 0b00010, 0b00010, 0b00010, 0b10010, 0b01100},  // 74 J
    {0b10001, 0b10010, 0b10100, 0b11000, 0b10100, 0b10010, 0b10001},  // 75 K
    {0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b10000, 0b11111},  // 76 L
    {0b10001, 0b11011, 0b10101, 0b10101, 0b10001, 0b10001, 0b10001},  // 77 M
    {0b10001, 0b10001, 0b11001, 0b10101, 0b10011, 0b10001, 0b10001},  // 78 N
    {0b01110, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110},  // 79 O
    {0b11110, 0b10001, 0b10001, 0b11110, 0b10000, 0b10000, 0b10000},  // 80 P
    {0b01110, 0b10001, 0b10001, 0b10001, 0b10101, 0b10010, 0b01101},  // 81 Q
    {0b11110, 0b10001, 0b10001, 0b11110, 0b10100, 0b10010, 0b10001},  // 82 R
    {0b01111, 0b10000, 0b10000, 0b01110, 0b00001, 0b00001, 0b11110},  // 83 S
    {0b11111, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100},  // 84 T
    {0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01110},  // 85 U
    {0b10001, 0b10001, 0b10001, 0b10001, 0b10001, 0b01010, 0b00100},  // 86 V
    {0b10001, 0b10001, 0b10001, 0b10101, 0b10101, 0b11011, 0b10001},  // 87 W
    {0b10001, 0b10001, 0b01010, 0b00100, 0b01010, 0b10001, 0b10001},  // 88 X
    {0b10001, 0b10001, 0b01010, 0b00100, 0b00100, 0b00100, 0b00100},  // 89 Y
    {0b11111, 0b00001, 0b00010, 0b00100, 0b01000, 0b10000, 0b11111},  // 90 Z
    {0b01110, 0b01000, 0b01000, 0b01000, 0b01000, 0b01000, 0b01110},  // 91 '['
    {0b10000, 0b10000, 0b01000, 0b00100, 0b00010, 0b00001, 0b00001},  // 92 '\'
    {0b01110, 0b00010, 0b00010, 0b00010, 0b00010, 0b00010, 0b01110},  // 93 ']'
    {0b00100, 0b01010, 0b10001, 0b00000, 0b00000, 0b00000, 0b00000},  // 94 '^'
    {0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000, 0b11111},  // 95 '_'
    {0b01000, 0b00100, 0b00000, 0b00000, 0b00000, 0b00000, 0b00000},  // 96 '`'
    {0b00000, 0b00000, 0b01110, 0b00001, 0b01111, 0b10001, 0b01111},  // 97 a
    {0b10000, 0b10000, 0b11110, 0b10001, 0b10001, 0b10001, 0b11110},  // 98 b
    {0b00000, 0b00000, 0b01110, 0b10000, 0b10000, 0b10000, 0b01110},  // 99 c
    {0b00001, 0b00001, 0b01111, 0b10001, 0b10001, 0b10001, 0b01111},  // 100 d
    {0b00000, 0b00000, 0b01110, 0b10001, 0b11111, 0b10000, 0b01110},  // 101 e
    {0b00110, 0b01001, 0b01000, 0b11100, 0b01000, 0b01000, 0b01000},  // 102 f
    {0b00000, 0b01111, 0b10001, 0b10001, 0b01111, 0b00001, 0b01110},  // 103 g
    {0b10000, 0b10000, 0b11110, 0b10001, 0b10001, 0b10001, 0b10001},  // 104 h
    {0b00100, 0b00000, 0b01100, 0b00100, 0b00100, 0b00100, 0b01110},  // 105 i
    {0b00010, 0b00000, 0b00110, 0b00010, 0b00010, 0b10010, 0b01100},  // 106 j
    {0b10000, 0b10000, 0b10010, 0b10100, 0b11000, 0b10100, 0b10010},  // 107 k
    {0b01100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b01110},  // 108 l
    {0b00000, 0b00000, 0b11010, 0b10101, 0b10101, 0b10101, 0b10101},  // 109 m
    {0b00000, 0b00000, 0b11110, 0b10001, 0b10001, 0b10001, 0b10001},  // 110 n
    {0b00000, 0b00000, 0b01110, 0b10001, 0b10001, 0b10001, 0b01110},  // 111 o
    {0b00000, 0b00000, 0b11110, 0b10001, 0b10001, 0b11110, 0b10000},  // 112 p
    {0b00000, 0b00000, 0b01111, 0b10001, 0b10001, 0b01111, 0b00001},  // 113 q
    {0b00000, 0b00000, 0b10110, 0b11000, 0b10000, 0b10000, 0b10000},  // 114 r
    {0b00000, 0b00000, 0b01111, 0b10000, 0b01110, 0b00001, 0b11110},  // 115 s
    {0b01000, 0b01000, 0b11100, 0b01000, 0b01000, 0b01001, 0b01100},  // 116 t
    {0b00000, 0b00000, 0b10001, 0b10001, 0b10001, 0b10001, 0b01111},  // 117 u
    {0b00000, 0b00000, 0b10001, 0b10001, 0b10001, 0b01010, 0b00100},  // 118 v
    {0b00000, 0b00000, 0b10001, 0b10101, 0b10101, 0b10101, 0b01010},  // 119 w
    {0b00000, 0b00000, 0b10001, 0b01010, 0b00100, 0b01010, 0b10001},  // 120 x
    {0b00000, 0b10001, 0b10001, 0b10001, 0b01111, 0b00001, 0b01110},  // 121 y
    {0b00000, 0b00000, 0b11111, 0b00010, 0b00100, 0b01000, 0b11111},  // 122 z
    {0b00110, 0b01000, 0b01000, 0b11000, 0b01000, 0b01000, 0b00110},  // 123 '{'
    {0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100, 0b00100},  // 124 '|'
    {0b01100, 0b00010, 0b00010, 0b00001, 0b00010, 0b00010, 0b01100},  // 125 '}'
    {0b00000, 0b00000, 0b01000, 0b10101, 0b00010, 0b00000, 0b00000},  // 126 '~'
}};

// The glyph of `c`, or the space glyph for anything outside the table range.
// A plain lookup keeps the renderer free of branches on text content.
constexpr const Glyph& glyphFor(char c) {
  const unsigned code = static_cast<unsigned char>(c);
  return kGlyphTable[code < static_cast<unsigned>(kFirstGlyphChar) ||
                             code > static_cast<unsigned>(kLastGlyphChar)
                         ? 0
                         : code - kFirstGlyphChar];
}

constexpr bool glyphPixel(const Glyph& glyph, int column, int row) {
  return (glyph[row] & (1u << (kGlyphColumns - 1 - column))) != 0;
}

// Width of `text` in pixels at `scale` (one font pixel becomes a scale x scale
// square). The trailing spacing column of the last glyph does not count.
constexpr int textWidth(int scale, const char* text) {
  if (text == nullptr) {
    return 0;
  }
  int characters = 0;
  for (const char* p = text; *p != '\0'; ++p) {
    ++characters;
  }
  return characters == 0 ? 0
                         : characters * kGlyphAdvance * scale - kGlyphSpacing * scale;
}

constexpr int textWidth(const char* text) { return textWidth(1, text); }

}  // namespace stackfall
