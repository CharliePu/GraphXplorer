#include <atomic>
#include <iostream>

#include "Core/Application.h"
#include "Util/PerformanceProfiler.h"

int main()
{
    std::shared_ptr<Application> app;
    {
        GRAPHX_PROFILE_SCOPE("main.createApplication");
        app = std::make_shared<Application>(800, 800, "GraphXplorer");
    }

    {
        GRAPHX_PROFILE_SCOPE("main.run");
        app->run();
    }

    return 0;
}
