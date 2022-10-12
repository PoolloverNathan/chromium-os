// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "backend/test/backend_test.h"

namespace cros_im {
namespace test {

BACKEND_TEST(GtkSetSurroundingTextTest, BasicTextInput) {
  ExpectCreateTextInput(CreateTextInputOptions::kIgnoreCommon);
  Unignore(Request::kSetSurroundingText);

  ExpectSetSurroundingText("", 0, 0);
  Expect(Request::kActivate);
  ExpectSetSurroundingText("", 0, 0);
  ExpectSetSurroundingText("", 0, 0);

  SendCommitString("a");
  ExpectSetSurroundingText("a", 1, 1);

  SendCommitString("bc");
  ExpectSetSurroundingText("abc", 3, 3);

  // 3 bytes in UTF-8
  SendCommitString("あ");
  ExpectSetSurroundingText("abcあ", 6, 6);

  SendCommitString("z");
  ExpectSetSurroundingText("abcあz", 7, 7);

  Expect(Request::kDeactivate);
  Expect(Request::kDestroy);
}

BACKEND_TEST(GtkSetSurroundingTextTest, CursorMovement) {
  ExpectCreateTextInput(CreateTextInputOptions::kIgnoreCommon);
  Unignore(Request::kSetSurroundingText);

  ExpectSetSurroundingText("", 0, 0);
  Expect(Request::kActivate);
  ExpectSetSurroundingText("", 0, 0);
  ExpectSetSurroundingText("", 0, 0);

  // ñ is 2 bytes in UTF-8
  SendCommitString("piñata");
  ExpectSetSurroundingText("piñata", 7, 7);

  // Moving the cursor causes a reset in gtk_text_view_mark_set_handler(), but
  // only the first time apparently.
  ExpectSetSurroundingText("piñata", 4, 4);
  Expect(Request::kReset);

  ExpectSetSurroundingText("piñata", 2, 2);
  ExpectSetSurroundingText("piñata", 0, 0);
  ExpectSetSurroundingText("piñata", 6, 6);

  Expect(Request::kDeactivate);
  Expect(Request::kDestroy);
}

BACKEND_TEST(GtkSetSurroundingTextTest, MultiLine) {
  ExpectCreateTextInput(CreateTextInputOptions::kIgnoreCommon);
  Unignore(Request::kSetSurroundingText);

  ExpectSetSurroundingText("", 0, 0);
  Expect(Request::kActivate);
  ExpectSetSurroundingText("", 0, 0);
  ExpectSetSurroundingText("", 0, 0);

  // GTK only gives us the current line for surrounding text.

  SendCommitString("line 1\nline 2\nline 3");
  ExpectSetSurroundingText("line 3", 6, 6);

  // Moving the cursor causes a reset in gtk_text_view_mark_set_handler(), but
  // only the first time apparently.
  ExpectSetSurroundingText("line 1", 5, 5);
  Expect(Request::kReset);

  ExpectSetSurroundingText("line 2", 0, 0);
  ExpectSetSurroundingText("line 1", 0, 0);
  ExpectSetSurroundingText("line 3", 3, 3);

  Expect(Request::kDeactivate);
  Expect(Request::kDestroy);
}

BACKEND_TEST(GtkSetSurroundingTextTest, DirectTextChanges) {
  // Test that text changes made directly by an app (not as a result of user
  // input) are reported to us.

  ExpectCreateTextInput(CreateTextInputOptions::kIgnoreCommon);
  Unignore(Request::kSetSurroundingText);

  ExpectSetSurroundingText("", 0, 0);
  Expect(Request::kActivate);
  ExpectSetSurroundingText("", 0, 0);

  ExpectSetSurroundingText("smart", 5, 5);
  ExpectSetSurroundingText("trams", 5, 5);
  ExpectSetSurroundingText("soufflé", 8, 8);

  Expect(Request::kDeactivate);
  Expect(Request::kDestroy);
}

}  // namespace test
}  // namespace cros_im