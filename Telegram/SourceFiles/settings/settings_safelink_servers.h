#pragma once

namespace Window {
class Controller;
class SessionController;
} // namespace Window

namespace Settings {

void ShowServersBox(not_null<Window::SessionController*> controller);
void ShowAddServerBox(not_null<Window::SessionController*> controller);
void ShowServersBox(not_null<Window::Controller*> controller);
void ShowAddServerBox(not_null<Window::Controller*> controller);

} // namespace Settings
