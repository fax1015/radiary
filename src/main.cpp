#include <windows.h>

#include "app/AppWindow.h"

int WINAPI wWinMain(HINSTANCE instance, HINSTANCE, PWSTR, int showCommand) {
    radiary::AppWindow app;
    if (!app.Create(instance, showCommand)) {
        return 1;
    }
    return app.Run();
}
