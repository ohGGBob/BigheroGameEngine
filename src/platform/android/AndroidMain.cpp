// Android 入口：native_app_glue 驱动的 NativeActivity 主流程（v1 单会话）。
// 流程：等待 INIT_WINDOW -> APK 资源落地 -> 构造 Application（经 Window 工厂获得
// AndroidWindow）-> 进入引擎主循环 -> 结束后请求 finish Activity。
// 退后台（TERM_WINDOW）/系统销毁（DESTROY）会终止主循环并退出进程。
#include <android/log.h>
#include <android/native_activity.h>
#include <android_native_app_glue.h>

#include "app/Application.h"
#include "platform/android/AndroidAssets.h"
#include "platform/android/AndroidWindow.h"

#define BH_ANDROID_LOG(...) __android_log_print(ANDROID_LOG_INFO, "BigHeroEngine", __VA_ARGS__)
#define BH_ANDROID_ERR(...) __android_log_print(ANDROID_LOG_ERROR, "BigHeroEngine", __VA_ARGS__)

void android_main(struct android_app* state)
{
    BH_ANDROID_LOG("android_main 启动");

    // 注册 glue 回调（转发到 AndroidWindow 单例）+ 会话句柄（Window 工厂读取）
    state->onAppCmd = &BigHero::AndroidWindow::HandleAppCmd;
    state->onInputEvent = &BigHero::AndroidWindow::HandleInputEvent;
    BigHero::SetAndroidAppSession(state);

    // 阻塞等待窗口就绪或进程销毁
    while (state->destroyRequested == 0 && state->window == nullptr)
    {
        int events = 0;
        android_poll_source* source = nullptr;
        const int ident = static_cast<int>(ALooper_pollOnce(-1, nullptr, &events, reinterpret_cast<void**>(&source)));
        if (ident >= 0 && source != nullptr)
            source->process(state, source);
    }
    if (state->destroyRequested != 0)
        return;

    // APK 资源落地 + chdir（引擎相对路径依赖）
    BigHero::PrepareAndroidAssets(state);

    // 引擎主循环：窗口尺寸由 ANativeWindow 决定（config 中的默认值仅桌面生效）
    BigHero::Application::AppConfig config;
    try
    {
        BigHero::Application app(config);
        const int exitCode = app.Run();
        BH_ANDROID_LOG("主循环结束，退出码 %d", exitCode);
    }
    catch (const std::exception& e)
    {
        BH_ANDROID_ERR("未捕获异常: %s", e.what());
    }

    // 单会话结束：请求关闭 Activity（TERM/DESTROY 时进程随后被系统回收）
    ANativeActivity_finish(state->activity);
}
