// keysdialog.cpp -- the Key Bindings dialog.
#include "keysdialog.h"

#include <wx/button.h>
#include <wx/dialog.h>
#include <wx/msgdlg.h>
#include <wx/notebook.h>
#include <wx/panel.h>
#include <wx/settings.h>
#include <wx/sizer.h>
#include <wx/statbox.h>
#include <wx/stattext.h>

#include <vector>

namespace {

class KeysDialog : public wxDialog {
public:
    KeysDialog(wxWindow* parent, const keys::Bindings& bindings)
        : wxDialog(parent, wxID_ANY, "Key Bindings", wxDefaultPosition, wxDefaultSize, wxDEFAULT_DIALOG_STYLE),
          edited_(bindings) {
        const int margin = FromDIP(12), gap = FromDIP(8), small = FromDIP(4);

        auto* intro = new wxStaticText(this, wxID_ANY,
                                       "Click a key, then press the new one. Keys are the keyboard's places, "
                                       "whatever its layout.\nChanged keys are shown in bold.");
        intro->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));

        // A page per group, the actions in two columns of label and key button.
        auto* book = new wxNotebook(this, wxID_ANY);
        for (int g = 0; g < keys::GROUP_COUNT; ++g) {
            std::vector<int> mine;
            for (int i = 0; i < keys::ACTION_COUNT; ++i)
                if (keys::ACTIONS[i].group == g) mine.push_back(i);
            auto* page = new wxPanel(book);
            auto* grid = new wxFlexGridSizer(4, small, gap);
            const int rows = static_cast<int>(mine.size() + 1) / 2;
            for (int r = 0; r < rows; ++r) {
                for (int c = 0; c < 2; ++c) {
                    const int at = r + c * rows;
                    if (at >= static_cast<int>(mine.size())) {
                        grid->AddSpacer(0);
                        grid->AddSpacer(0);
                        continue;
                    }
                    const int i = mine[at];
                    grid->Add(new wxStaticText(page, wxID_ANY, keys::ACTIONS[i].label), 0,
                              wxALIGN_CENTER_VERTICAL | (c ? wxLEFT : 0), margin);
                    auto* button = new wxButton(page, wxID_ANY, wxEmptyString, wxDefaultPosition,
                                                wxSize(FromDIP(120), -1));
                    button->Bind(wxEVT_BUTTON, [this, i](wxCommandEvent&) { Change(i); });
                    grid->Add(button, 0, wxALIGN_CENTER_VERTICAL);
                    buttons_.resize(keys::ACTION_COUNT);
                    buttons_[i] = button;
                }
            }
            auto* pageSizer = new wxBoxSizer(wxVERTICAL);
            pageSizer->Add(grid, 0, wxALL, gap);
            page->SetSizer(pageSizer);
            book->AddPage(page, keys::GROUP_TITLES[g]);
        }

        auto* fixedBox = new wxStaticBoxSizer(wxVERTICAL, this, "Keys that keep their job");
        wxWindow* fb = fixedBox->GetStaticBox();
        auto* fixedGrid = new wxFlexGridSizer(2, small, FromDIP(16));
        for (int i = 0; i < keys::FIXED_COUNT; ++i) {
            fixedGrid->Add(new wxStaticText(fb, wxID_ANY, keys::FIXED[i].keys));
            auto* what = new wxStaticText(fb, wxID_ANY, keys::FIXED[i].what);
            what->SetForegroundColour(wxSystemSettings::GetColour(wxSYS_COLOUR_GRAYTEXT));
            fixedGrid->Add(what);
        }
        fixedBox->Add(fixedGrid, 0, wxALL, gap);

        auto* row = new wxBoxSizer(wxHORIZONTAL);
        auto* reset = new wxButton(this, wxID_ANY, "&Reset to defaults");
        reset->SetToolTip("Put every key back as the game has it.");
        reset->Bind(wxEVT_BUTTON, [this](wxCommandEvent&) {
            edited_ = keys::Defaults();
            ShowKeys();
        });
        row->Add(reset, 0, wxALIGN_CENTER_VERTICAL);
        row->AddStretchSpacer();
        row->Add(CreateStdDialogButtonSizer(wxOK | wxCANCEL), 0, wxALIGN_CENTER_VERTICAL);

        auto* all = new wxBoxSizer(wxVERTICAL);
        all->Add(intro, 0, wxLEFT | wxRIGHT | wxTOP, margin);
        all->Add(book, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, margin);
        all->Add(fixedBox, 0, wxEXPAND | wxLEFT | wxRIGHT | wxTOP, margin);
        all->Add(row, 0, wxEXPAND | wxALL, margin);
        SetSizerAndFit(all);
        ShowKeys();
        CentreOnParent();
    }

    const keys::Bindings& Edited() const { return edited_; }

private:
    // Shows each action's key on its button.
    void ShowKeys() {
        for (int i = 0; i < keys::ACTION_COUNT; ++i) {
            const wxString name = keys::Name(edited_[i]);
            wxString label = name;
            label.Replace("&", "&&");  // buttons treat '&' as a mnemonic marker
            buttons_[i]->SetLabel(label);
            buttons_[i]->SetToolTip(wxString::Format("%s: %s. Click to change it.", keys::ACTIONS[i].label, name));
            wxFont font = buttons_[i]->GetParent()->GetFont();
            if (edited_[i] != keys::ACTIONS[i].def) font.MakeBold();
            buttons_[i]->SetFont(font);
        }
    }

    void Change(int action) {
        const wxString label = keys::ACTIONS[action].label;
        const int key = keys::AskKey(this, label);
        if (key < 0 || key == edited_[action]) return;
        if (key != 0) {
            for (int other = 0; other < keys::ACTION_COUNT; ++other) {
                if (other == action || edited_[other] != key) continue;
                const wxString question = wxString::Format(
                    "%s is already the key for %s.\n\nUse it for %s instead? %s will have no key.", keys::Name(key),
                    keys::ACTIONS[other].label, label, keys::ACTIONS[other].label);
                if (wxMessageBox(question, "Key Bindings", wxYES_NO | wxICON_QUESTION, this) != wxYES) return;
                edited_[other] = 0;
            }
        }
        edited_[action] = key;
        ShowKeys();
    }

    keys::Bindings edited_;
    std::vector<wxButton*> buttons_;
};

}  // namespace

bool EditKeyBindings(wxWindow* parent, keys::Bindings& bindings) {
    KeysDialog dialog(parent, bindings);
    if (dialog.ShowModal() != wxID_OK) return false;
    bindings = dialog.Edited();
    return true;
}
