#include <ECDI/Application/Application.h>
#include <ECDI/Window/Window.h>
#include <ECDI/Widget/Panel.h>

#include <memory>

int WINAPI wWinMain(HINSTANCE, HINSTANCE, PWSTR, int)
{
	ECDI::Application application;
	ECDI::Window& window = application.Create("Minimal ECDI Consumer", 480, 320);
	auto panel = std::make_unique<ECDI::Panel>();
	panel->SetStretch(1);
	window.GetRootWidget().AddChild(std::move(panel));
	window.GetRootWidget().Arrange();
	window.Show();
	return application.Run();
}
