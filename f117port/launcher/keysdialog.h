// keysdialog.h -- the Key Bindings dialog: one button per action showing its key; click it, then press the new key.
#pragma once

#include "keys.h"

class wxWindow;

// Shows the dialog on `bindings`. Returns true, with `bindings` changed, if the user pressed OK.
bool EditKeyBindings(wxWindow* parent, keys::Bindings& bindings);
