#pragma once

namespace Window {
class SessionController;
} // namespace Window

namespace Settings {

void ShowServersBox(not_null<Window::SessionController*> controller);
void ShowAddServerBox(not_null<Window::SessionController*> controller);

} // namespace Settings
