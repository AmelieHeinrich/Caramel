/**
 * @ Author: Amélie Heinrich (amelie@dayiii.com)
 * @ Create Time: 2026-08-01 20:43:02
 * @ Copyright: Day III Digital - All rights reserved
 */

#include "Application.hpp"

int main(void)
{
    ApplicationInfo info;
    info.Width = 1280;
    info.Height = 720;
    info.Maximized = true;
    info.VSync = true;
    info.DebugLayer = true;

    Application app(info);
    app.Run();
    return 0;
}
