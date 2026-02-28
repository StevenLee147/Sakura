#include <SDL3/SDL_main.h>
#include "core/app.h"

int main(int /*argc*/, char* /*argv*/[])
{
    sakura::core::App app;
    if (app.Initialize())
    {
        app.Run();
    }
    app.Shutdown();
    return 0;
}
