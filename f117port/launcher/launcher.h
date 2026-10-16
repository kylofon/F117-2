// launcher.h -- the launcher window: choose the game folder and the display and sound options, then Play.
#pragma once

#include <wx/dialog.h>

#include "keys.h"

class wxButton;
class wxCheckBox;
class wxChoice;
class wxSpinCtrl;
class wxStaticBitmap;
class wxStaticText;
class wxTextCtrl;

extern const char* const APP_TITLE;

class LauncherDialog : public wxDialog {
public:
    LauncherDialog();

private:
    // Checks the folder and the program and enables Play to match.
    void UpdateState();
    // Relabels the window sizes for the chosen aspect.
    void FillScales();
    void BrowseFolder();
    void BrowseProgram();
    void Play();
    void About();
    void EditKeys();
    void Save();

    wxTextCtrl* folder_ = nullptr;
    wxTextCtrl* program_ = nullptr;
    wxStaticBitmap* statusIcon_ = nullptr;
    wxStaticText* statusNote_ = nullptr;
    wxChoice* aspect_ = nullptr;
    wxChoice* scale_ = nullptr;
    wxCheckBox* fullscreen_ = nullptr;
    wxCheckBox* sound_ = nullptr;
    wxSpinCtrl* speed_ = nullptr;
    wxButton* play_ = nullptr;
    keys::Bindings bindings_;  // the game's keys (Key Bindings...), saved with the other options
    bool loading_ = true;  // no edits are recorded while the window is built
};
