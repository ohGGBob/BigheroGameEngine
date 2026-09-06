#include "app/Application.h"

#include <cstring>
#include <iostream>

int main(int argc, char* argv[])
{
    // 崩溃时不丢日志：stdout 改为无缓冲（崩溃前最后一条日志可见）
    setvbuf(stdout, nullptr, _IONBF, 0);

    BigHero::Application::AppConfig config;

    for (int i = 1; i < argc; ++i)
    {
        if (std::strcmp(argv[i], "--headless") == 0)
        {
            config.headless = true;
        }
        else if (std::strcmp(argv[i], "--validate-only") == 0)
        {
            config.validateOnly = true;
        }
        else if (std::strcmp(argv[i], "--width") == 0 && i + 1 < argc)
        {
            config.width = static_cast<uint32_t>(std::stoul(argv[++i]));
        }
        else if (std::strcmp(argv[i], "--height") == 0 && i + 1 < argc)
        {
            config.height = static_cast<uint32_t>(std::stoul(argv[++i]));
        }
        else if (std::strcmp(argv[i], "--title") == 0 && i + 1 < argc)
        {
            config.title = argv[++i];
        }
        else if (std::strcmp(argv[i], "--help") == 0)
        {
            std::cout << "BigHero Engine - Vulkan\n";
            std::cout << "Usage: " << argv[0] << " [options]\n";
            std::cout << "Options:\n";
            std::cout << "  --headless         Run without window (for CI)\n";
            std::cout << "  --validate-only    Initialize Vulkan + pipelines and exit\n";
            std::cout << "  --width <w>        Window width (default: 1600)\n";
            std::cout << "  --height <h>       Window height (default: 900)\n";
            std::cout << "  --title <t>        Window title\n";
            std::cout << "  --help             Show this help\n";
            return 0;
        }
    }

    if (config.validateOnly)
    {
        BigHero::Application app(config);
        return app.ValidateOnly();
    }

    BigHero::Application app(config);
    return app.Run();
}