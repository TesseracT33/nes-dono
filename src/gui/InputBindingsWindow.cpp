#include "InputBindingsWindow.h"

#define SDL_KEYCODE_NOT_FOUND -1

wxBEGIN_EVENT_TABLE(InputBindingsWindow, wxFrame)
	EVT_BUTTON(ButtonID::set_to_keyboard_defaults, InputBindingsWindow::OnResetKeyboard)
	EVT_BUTTON(ButtonID::set_to_joypad_defaults, InputBindingsWindow::OnResetJoypad)
	EVT_BUTTON(ButtonID::cancel_and_exit, InputBindingsWindow::OnCancelAndExit)
	EVT_BUTTON(ButtonID::save_and_exit, InputBindingsWindow::OnSaveAndExit)
	EVT_BUTTON(ButtonID::unbind_p1, InputBindingsWindow::OnUnbindAll)
	EVT_BUTTON(ButtonID::unbind_p2, InputBindingsWindow::OnUnbindAll)
	EVT_CLOSE(InputBindingsWindow::OnCloseWindow)
wxEND_EVENT_TABLE()


InputBindingsWindow::InputBindingsWindow(wxWindow* parent, Config* config, Joypad* joypad, bool* window_active) :
	wxFrame(parent, wxID_ANY, "Input binding configuration", wxDefaultPosition, wxDefaultSize,
		wxDEFAULT_FRAME_STYLE & ~(wxRESIZE_BORDER | wxMAXIMIZE_BOX))
{
	this->config = config;
	this->joypad = joypad;
	this->window_active = window_active;

	// determine and set size of window
	int sizeX = 2 * padding + label_size.x + 2 * button_bind_size.x + controller_image_size.GetWidth();
	int sizeY = std::max(2 * padding + label_size.y + (num_input_keys + 2) * button_bind_size.y, (int)controller_image_size.GetHeight());
	SetClientSize(sizeX, sizeY);

	// create and layout all labels and buttons
	static_text_control = new wxStaticText(this, wxID_ANY, "Control", wxPoint(padding                                    , 0), label_size);
	static_text_bind_p1 = new wxStaticText(this, wxID_ANY, "Bind P1", wxPoint(padding + label_size.x                     , 0), label_size);
	static_text_bind_p1 = new wxStaticText(this, wxID_ANY, "Bind P2", wxPoint(padding + label_size.x + button_bind_size.x, 0), label_size);

	for (int i = 0; i < num_input_keys; i++)
	{
		static_text_buttons[i] = new wxStaticText(this, wxID_ANY                   , button_labels[i], wxPoint(padding                                    , label_size.y + label_size.y       * i), label_size      );
		buttons_p1         [i] = new wxButton    (this, ButtonID::bind_start_p1 + i, button_labels[i], wxPoint(padding + label_size.x                     , label_size.y + button_bind_size.y * i), button_bind_size);
		buttons_p2         [i] = new wxButton    (this, ButtonID::bind_start_p2 + i, button_labels[i], wxPoint(padding + label_size.x + button_bind_size.x, label_size.y + button_bind_size.y * i), button_bind_size);
	}

	unsigned end_of_input_buttons_y = padding + label_size.y + std::max(label_size.y * num_input_keys, button_bind_size.y * num_input_keys);
	button_set_to_keyboard_defaults = new wxButton(this, ButtonID::set_to_keyboard_defaults, "Reset to keyboard defaults", wxPoint(padding                            , end_of_input_buttons_y                        ), button_options_size);
	button_set_to_joypad_defaults   = new wxButton(this, ButtonID::set_to_joypad_defaults  , "Reset to joypad defaults"  , wxPoint(padding +     button_options_size.x, end_of_input_buttons_y                        ), button_options_size);
	button_unbind_p1                = new wxButton(this, ButtonID::unbind_p1               , "Unbind player 1"           , wxPoint(padding + 2 * button_options_size.x, end_of_input_buttons_y                        ), button_options_size);
	button_cancel_and_exit          = new wxButton(this, ButtonID::cancel_and_exit         , "Cancel and exit"           , wxPoint(padding                            , end_of_input_buttons_y + button_options_size.y), button_options_size);
	button_save_and_exit            = new wxButton(this, ButtonID::save_and_exit           , "Save and exit"             , wxPoint(padding +     button_options_size.x, end_of_input_buttons_y + button_options_size.y), button_options_size);
	button_unbind_p2                = new wxButton(this, ButtonID::unbind_p2               , "Unbind player 2"           , wxPoint(padding + 2 * button_options_size.x, end_of_input_buttons_y + button_options_size.y), button_options_size);

	// setup image
	{
		wxLogNull logNo; // stops wxwidgets from logging a message about the png when the window is opened
		wxImage::AddHandler(new wxPNGHandler());
		controller_image = new wxStaticBitmap(this, wxID_ANY, wxBitmap(controller_image_path, wxBITMAP_TYPE_PNG), wxPoint(2 * padding + label_size.x + 2 * button_bind_size.x, 0));
	}

	GetAndSetButtonLabels();

	SetBackgroundColour(*wxWHITE);

	// setup event bindings
	for (int i = 0; i < num_input_keys; i++)
	{
		// https://wiki.wxwidgets.org/Catching_key_events_globally
		buttons_p1[i]->Bind(wxEVT_CHAR_HOOK, &InputBindingsWindow::OnKeyDown, this);
		buttons_p1[i]->Bind(wxEVT_JOY_BUTTON_DOWN, &InputBindingsWindow::OnJoyDown, this);
		buttons_p1[i]->Bind(wxEVT_BUTTON, &InputBindingsWindow::OnInputButtonPress, this);
		buttons_p1[i]->Bind(wxEVT_KILL_FOCUS, &InputBindingsWindow::OnButtonLostFocus, this);

		buttons_p2[i]->Bind(wxEVT_CHAR_HOOK, &InputBindingsWindow::OnKeyDown, this);
		buttons_p2[i]->Bind(wxEVT_JOY_BUTTON_DOWN, &InputBindingsWindow::OnJoyDown, this);
		buttons_p2[i]->Bind(wxEVT_BUTTON, &InputBindingsWindow::OnInputButtonPress, this);
		buttons_p2[i]->Bind(wxEVT_KILL_FOCUS, &InputBindingsWindow::OnButtonLostFocus, this);
	}

	*window_active = true;
}


void InputBindingsWindow::OnInputButtonPress(wxCommandEvent& event)
{
	int button_index = event.GetId() - ButtonID::bind_start_p1;
	Joypad::Player player = static_cast<Joypad::Player>(button_index / num_input_keys);
	if (player == Joypad::Player::ONE)
	{
		prev_input_button_label = buttons_p1[button_index]->GetLabel();
		buttons_p1[button_index]->SetLabel("...");
	}
	else
	{
		prev_input_button_label = buttons_p2[button_index]->GetLabel();
		buttons_p2[button_index]->SetLabel("...");
	}

	index_of_awaited_input_button = button_index;
	awaiting_input = true;
}


void InputBindingsWindow::OnKeyDown(wxKeyEvent& event)
{
	if (awaiting_input)
	{
		awaiting_input = false;

		int keycode = event.GetKeyCode();
		if (keycode != WXK_NONE)
		{
			SDL_Keycode sdl_keycode = Convert_WX_Keycode_To_SDL_Keycode(keycode);
			if (sdl_keycode != SDL_KEYCODE_NOT_FOUND)
			{
				const char* name = SDL_GetKeyName(sdl_keycode);
				buttons_p1[index_of_awaited_input_button]->SetLabel(name);
				//joypad->UpdateBinding(static_cast<Joypad::Button>(index_of_awaited_input_button), sdl_keycode);
				CheckForDuplicateBindings(name);
				return;
			}
		}

		buttons_p1[index_of_awaited_input_button]->SetLabel(prev_input_button_label);
	}
}


void InputBindingsWindow::OnJoyDown(wxJoystickEvent& event)
{
	if (awaiting_input)
	{
		awaiting_input = false;

		int keycode = 0; // todo
		if (keycode != WXK_NONE)
		{
			SDL_Keycode sdl_keycode = Convert_WX_Keycode_To_SDL_Keycode(keycode);
			if (sdl_keycode != SDL_KEYCODE_NOT_FOUND)
			{
				const char* name = SDL_GetKeyName(sdl_keycode);
				buttons_p1[index_of_awaited_input_button]->SetLabel(name);
				//joypad->UpdateBinding(static_cast<Joypad::Button>(index_of_awaited_input_button), sdl_keycode);
				CheckForDuplicateBindings(name);
				return;
			}
		}

		buttons_p1[index_of_awaited_input_button]->SetLabel(prev_input_button_label);
	}
}


void InputBindingsWindow::OnButtonLostFocus(wxFocusEvent& event)
{
	if (awaiting_input)
	{
		buttons_p1[index_of_awaited_input_button]->SetLabel(prev_input_button_label);
		awaiting_input = false;
	}
}


void InputBindingsWindow::OnResetKeyboard(wxCommandEvent& event)
{
	//joypad->ResetBindings(Joypad::InputMethod::KEYBOARD);
	GetAndSetButtonLabels();
}


void InputBindingsWindow::OnResetJoypad(wxCommandEvent& event)
{
	//joypad->ResetBindings(Joypad::InputMethod::JOYPAD);
	GetAndSetButtonLabels();
}


void InputBindingsWindow::OnCancelAndExit(wxCommandEvent& event)
{
	joypad->RevertBindingChanges();
	Close();
	*window_active = false;
}


void InputBindingsWindow::OnSaveAndExit(wxCommandEvent& event)
{
	joypad->SaveBindings();
	config->Save();
	Close();
	*window_active = false;
}


void InputBindingsWindow::OnUnbindAll(wxCommandEvent& event)
{
	const wxString unbound_button_text = "Unbound";

	int id = event.GetId();
	if (id == ButtonID::unbind_p1)
	{
		for (int i = 0; i < num_input_keys; i++)
			buttons_p1[i]->SetLabel(unbound_button_text);
		joypad->UnbindAll(Joypad::Player::ONE);
	}
	else /* Player 2 */
	{
		for (int i = 0; i < num_input_keys; i++)
			buttons_p2[i]->SetLabel(unbound_button_text);
		joypad->UnbindAll(Joypad::Player::TWO);
	}
}


void InputBindingsWindow::OnCloseWindow(wxCloseEvent& event)
{
	joypad->RevertBindingChanges();
	event.Skip();
	*window_active = false;
}


void InputBindingsWindow::GetAndSetButtonLabels()
{
	for (int i = 0; i < num_input_keys; i++)
	{
		buttons_p1[i]->SetLabel(joypad->GetCurrentBindingString(static_cast<Joypad::Button>(Joypad::Button::A + i), Joypad::Player::ONE));
		buttons_p1[i]->SetLabel(joypad->GetCurrentBindingString(static_cast<Joypad::Button>(Joypad::Button::A + i), Joypad::Player::TWO));
	}
}


SDL_Keycode InputBindingsWindow::Convert_WX_Keycode_To_SDL_Keycode(int wx_keycode)
{
	// ASCII range. From what I have tested so far, the wxWidgets keycodes enum correspond to the SDL keycodes enum exactly in this range
	// One difference is that SDL does not define keycodes for uppercase letters, which is what wxwidgets produces by default.
	// If 'A' is pressed, then wx_keycode == 65. However, SDLK_a == 97, so we need to translate such key presses.
	if (wx_keycode >= 0 && wx_keycode <= 127)
	{
		if (wx_keycode >= 65 && wx_keycode <= 90)
			return (SDL_Keycode)(wx_keycode + 32);
		return (SDL_Keycode)wx_keycode;
	}
	// these keycodes in the wxWidgets keycode enum do not all have the same value as in the SDL keycode enum
	else
	{
		switch (wx_keycode)
		{
		case WXK_SHIFT: return SDLK_LSHIFT;
		case WXK_ALT: return SDLK_LALT;
		case WXK_CONTROL: return SDLK_LCTRL;
		case WXK_LEFT: return SDLK_LEFT;
		case WXK_UP: return SDLK_UP;
		case WXK_RIGHT: return SDLK_RIGHT;
		case WXK_DOWN: return SDLK_DOWN;

		case WXK_NUMPAD0: return SDLK_KP_0;
		case WXK_NUMPAD1: return SDLK_KP_1;
		case WXK_NUMPAD2: return SDLK_KP_2;
		case WXK_NUMPAD3: return SDLK_KP_3;
		case WXK_NUMPAD4: return SDLK_KP_4;
		case WXK_NUMPAD5: return SDLK_KP_5;
		case WXK_NUMPAD6: return SDLK_KP_6;
		case WXK_NUMPAD7: return SDLK_KP_7;
		case WXK_NUMPAD8: return SDLK_KP_8;
		case WXK_NUMPAD9: return SDLK_KP_9;
		case WXK_NUMPAD_ADD: return SDLK_KP_PLUS;
		case WXK_NUMPAD_SUBTRACT: return SDLK_KP_MINUS;
		case WXK_NUMPAD_MULTIPLY: return SDLK_KP_MULTIPLY;
		case WXK_NUMPAD_DIVIDE: return SDLK_KP_DIVIDE;
		case WXK_NUMPAD_DECIMAL: return SDLK_KP_DECIMAL;
		case WXK_NUMPAD_ENTER: return SDLK_KP_ENTER;

		case WXK_F1: return SDLK_F1;
		case WXK_F2: return SDLK_F2;
		case WXK_F3: return SDLK_F3;
		case WXK_F4: return SDLK_F4;
		case WXK_F5: return SDLK_F5;
		case WXK_F6: return SDLK_F6;
		case WXK_F7: return SDLK_F7;
		case WXK_F8: return SDLK_F8;
		case WXK_F9: return SDLK_F9;
		case WXK_F10: return SDLK_F10;
		case WXK_F11: return SDLK_F11;
		case WXK_F12: return SDLK_F12;

		default: return SDL_KEYCODE_NOT_FOUND;
		}
	}
}


u8 InputBindingsWindow::Convert_WX_Joybutton_To_SDL_Joybutton(int wx_joybutton)
{
	// TODO
	return 0;
}


void InputBindingsWindow::CheckForDuplicateBindings(const char* new_bound_key_name)
{
	// TODO
	for (int button_id = 0; button_id <= 2 * num_input_keys; button_id++)
	{

	}
}