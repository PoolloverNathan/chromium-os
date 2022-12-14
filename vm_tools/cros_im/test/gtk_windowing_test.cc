// Copyright 2022 The ChromiumOS Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "test/gtk_test_base.h"

#include <gtkmm/entry.h>
#include <gtkmm/popover.h>
#include <gtkmm/window.h>

// Tests involving multiple windows

namespace cros_im {
namespace test {

namespace {

// Popovers are transient windows, attached to a parent widget. This translates
// to a Wayland subsurface, so focus remains on the parent surface.
class GtkPopoverWindowTest : public GtkTestBase {
 public:
  GtkPopoverWindowTest() {
    window_.add(outer_entry_);
    outer_entry_.show();
    window_.show();

    popover_.add(inner_entry_);
    inner_entry_.show();
    popover_.set_relative_to(outer_entry_);
    // Don't show the popover yet
  }

 protected:
  Gtk::Window window_;
  Gtk::Entry outer_entry_;
  Gtk::Popover popover_;
  Gtk::Entry inner_entry_;
};

}  // namespace

TEST_F(GtkPopoverWindowTest, CommitString) {
  RunAndExpectBufferChangeTo(&outer_entry_, "ツ");
  popover_.show();
  RunAndExpectBufferChangeTo(&inner_entry_, "ü");
  popover_.hide();
  RunAndExpectBufferChangeTo(&outer_entry_, "ツ:)");
}

TEST_F(GtkPopoverWindowTest, KeySym) {
  RunAndExpectBufferChangeTo(&outer_entry_, "a");
  popover_.show();
  RunAndExpectBufferChangeTo(&inner_entry_, "ß");
  popover_.hide();
  RunAndExpectBufferChangeTo(&outer_entry_, "aœ");
  popover_.show();
  // Text is highlighted when re-showing so existing text is cleared on input.
  RunAndExpectBufferChangeTo(&inner_entry_, "p");
  // TODO(timloh): We should probably test control characters (e.g. bspc, esc)
  // in the popover but I wasn't able to make a non-flaky test.
}

}  // namespace test
}  // namespace cros_im
