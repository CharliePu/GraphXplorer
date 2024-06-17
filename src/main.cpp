#include <atomic>
#include <iostream>

#include "Core/Application.h"

int main()
{
    const auto app = std::make_shared<Application>(800, 800, "GraphXplorer");

    app->run();

    return 0;
}
