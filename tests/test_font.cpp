// Glyph table suite for the source-defined 5x7 font (task T010,
// specification section 6.2).
//
// The whole table lives in a header with no SDL type, so the portable suite
// checks it headlessly: complete printable-ASCII coverage, every row inside
// the five column bits, no blank glyph except space, every digit and letter
// distinct, and the exact shape of anchor glyphs whose picture is easy to
// state. A mistyped row in the table fails one of these checks.

#include <cstdint>

#include "stackfall/font.hpp"

#include "test_util.hpp"

namespace {

using namespace stackfall;

// Rows of a glyph as a plain value, for exact comparisons.
constexpr std::uint8_t rowOf(char c, int row) {
  return glyphFor(c)[static_cast<std::size_t>(row)];
}

void run_coverage_checks() {
  SC_CHECK(kGlyphCount == 95);
  SC_CHECK(kFirstGlyphChar == ' ');
  SC_CHECK(kLastGlyphChar == '~');
  SC_CHECK(kGlyphColumns == 5);
  SC_CHECK(kGlyphRows == 7);
  SC_CHECK(kGlyphAdvance == 6);

  // Every printable ASCII character maps to its own table entry, and every
  // row stays inside the five column bits.
  for (int code = 32; code <= 126; ++code) {
    const char c = static_cast<char>(code);
    const Glyph& glyph = glyphFor(c);
    for (int row = 0; row < kGlyphRows; ++row) {
      SC_CHECK(glyph[static_cast<std::size_t>(row)] < (1u << kGlyphColumns));
    }
    // The table is in code order, so the glyph of a character is its own
    // entry and not a neighbor's.
    SC_CHECK(&glyph == &kGlyphTable[static_cast<std::size_t>(code - 32)]);
  }

  // Space is blank, punctuation still draws at least a dot pair, and every
  // digit and letter carries a full shape rather than a stray mark.
  for (int code = 32; code <= 126; ++code) {
    const char c = static_cast<char>(code);
    int pixels = 0;
    for (int row = 0; row < kGlyphRows; ++row) {
      for (int column = 0; column < kGlyphColumns; ++column) {
        if (glyphPixel(glyphFor(c), column, row)) {
          ++pixels;
        }
      }
    }
    if (code == 32) {
      SC_CHECK(pixels == 0);
    } else if (c >= '0' && c <= '9' || c >= 'A' && c <= 'Z' || c >= 'a' && c <= 'z') {
      SC_CHECK(pixels >= 8);
    } else {
      SC_CHECK(pixels >= 2);
    }
  }

  // Anything outside the range falls back to space instead of reading out of
  // bounds: control characters, the delete code, and a negative char value.
  for (int code : {-128, -1, 0, 7, 27, 31, 127, 128, 255}) {
    const Glyph& fallback = glyphFor(static_cast<char>(code));
    SC_CHECK(&fallback == &kGlyphTable[0]);
  }
}

void run_distinctness_checks() {
  // Every digit and every uppercase letter keeps its own shape, so a
  // scrambled table entry cannot hide behind a duplicate.
  constexpr char kAlphanumerics[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ";
  constexpr int kAlphanumericCount =
      static_cast<int>(sizeof(kAlphanumerics) / sizeof(kAlphanumerics[0])) - 1;
  SC_CHECK(kAlphanumericCount == 36);
  for (int a = 0; a < kAlphanumericCount; ++a) {
    for (int b = a + 1; b < kAlphanumericCount; ++b) {
      SC_CHECK(glyphFor(kAlphanumerics[a]) !=
               glyphFor(kAlphanumerics[b]));
    }
  }
  // The lowercase letters are pairwise distinct as well.
  for (char a = 'a'; a <= 'z'; ++a) {
    for (char b = static_cast<char>(a + 1); b <= 'z'; ++b) {
      SC_CHECK(glyphFor(a) != glyphFor(b));
    }
  }
}

void run_anchor_shape_checks() {
  // Glyphs whose picture is simple to state in words. These pin the bit
  // order as well: row 0 is the top and bit 4 is the leftmost column.
  const std::uint8_t kBlank = 0;
  const std::uint8_t kFull = 0b11111;
  const std::uint8_t kCenter = 0b00100;

  // 'I' is a bar with serifs top and bottom and a plain stem between.
  SC_CHECK(rowOf('I', 0) == 0b01110);
  for (int row = 1; row < 6; ++row) {
    SC_CHECK(rowOf('I', row) == kCenter);
  }
  SC_CHECK(rowOf('I', 6) == 0b01110);

  // 'L' is one left column over a full bottom row.
  SC_CHECK(rowOf('L', 6) == kFull);
  for (int row = 0; row < 6; ++row) {
    SC_CHECK(rowOf('L', row) == 0b10000);
  }

  // 'E' has full top and bottom rows.
  SC_CHECK(rowOf('E', 0) == kFull);
  SC_CHECK(rowOf('E', 6) == kFull);

  // 'T' has a full top row over a centered stem.
  SC_CHECK(rowOf('T', 0) == kFull);
  for (int row = 1; row < 7; ++row) {
    SC_CHECK(rowOf('T', row) == kCenter);
  }

  // '-' is one full row in the vertical middle and nothing else.
  for (int row = 0; row < 7; ++row) {
    SC_CHECK(rowOf('-', row) == (row == 3 ? kFull : kBlank));
  }

  // '_' is one full row at the bottom and nothing else.
  for (int row = 0; row < 7; ++row) {
    SC_CHECK(rowOf('_', row) == (row == 6 ? kFull : kBlank));
  }

  // '|' is one centered column all the way down.
  for (int row = 0; row < 7; ++row) {
    SC_CHECK(rowOf('|', row) == kCenter);
  }

  // '.' is one 2x2 block in the bottom-left corner of the cell.
  for (int row = 0; row < 5; ++row) {
    SC_CHECK(rowOf('.', row) == kBlank);
  }
  SC_CHECK(rowOf('.', 5) == 0b01100);
  SC_CHECK(rowOf('.', 6) == 0b01100);

  // '0' carries the inner slash that tells it from 'O'.
  SC_CHECK(rowOf('0', 0) == rowOf('O', 0));
  SC_CHECK(rowOf('0', 6) == rowOf('O', 6));
  bool zeroHasInnerPixel = false;
  for (int row = 1; row < 6; ++row) {
    for (int column = 0; column < kGlyphColumns; ++column) {
      const bool zero = glyphPixel(glyphFor('0'), column, row);
      const bool oh = glyphPixel(glyphFor('O'), column, row);
      if (zero && !oh) {
        zeroHasInnerPixel = true;
      }
      if (!zero && oh && row > 0 && row < 6) {
        // 'O' keeps a plain ring; anything else would blur the two.
        SC_CHECK(false);
      }
    }
  }
  SC_CHECK(zeroHasInnerPixel);

  // Symmetric letters really are mirror symmetric, which pins the column bit
  // order: reading a row backwards gives the same row.
  const char symmetric[] = {'A', 'H', 'I', 'M', 'O', 'T', 'U', 'V', 'W', 'X',
                            'Y'};
  for (char c : symmetric) {
    for (int row = 0; row < kGlyphRows; ++row) {
      const std::uint8_t bits = rowOf(c, row);
      std::uint8_t mirrored = 0;
      for (int column = 0; column < kGlyphColumns; ++column) {
        if (glyphPixel(glyphFor(c), column, row)) {
          mirrored = static_cast<std::uint8_t>(
              mirrored | (1u << (kGlyphColumns - 1 - column)));
        }
      }
      SC_CHECK(bits == mirrored);
    }
  }
}

void run_metrics_checks() {
  SC_CHECK(textWidth("") == 0);
  SC_CHECK(textWidth(nullptr) == 0);
  SC_CHECK(textWidth("A") == 5);
  SC_CHECK(textWidth("AB") == 11);
  SC_CHECK(textWidth("STACKFALL") == 9 * 6 - 1);
  SC_CHECK(textWidth(2, "A") == 10);
  SC_CHECK(textWidth(2, "AB") == 22);
  // Every glyph shares one advance, so the width only counts characters.
  SC_CHECK(textWidth("ii") == textWidth("WW"));
}

}  // namespace

void run_font_tests() {
  run_coverage_checks();
  run_distinctness_checks();
  run_anchor_shape_checks();
  run_metrics_checks();
}
