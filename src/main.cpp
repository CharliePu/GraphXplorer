#include "Core/Application.h"

int main()
{
    const auto app = std::make_shared<Application>(1200, 800, "GraphXplorer");

    app->run();

    return 0;
}