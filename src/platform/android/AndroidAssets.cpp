// Android APK 资源落地实现：AAssetManager 递归拷贝 + chdir。
#include "platform/android/AndroidAssets.h"

#include "core/Log.h"

#include <android/asset_manager.h>
#include <android/asset_manager_jni.h>
#include <android/log.h>

#include <unistd.h>

#include <cstdio>
#include <filesystem>
#include <string>
#include <vector>

#define BH_ANDROID_LOG(...) __android_log_print(ANDROID_LOG_INFO, "BigHeroEngine", __VA_ARGS__)

namespace BigHero
{
namespace
{
const char* assetDirLabel(const char* dir)
{
    return (dir[0] == '\0') ? "<root>" : dir;
}

bool CopyAssetFile(AAssetManager* mgr, const std::string& assetPath, const std::filesystem::path& targetPath)
{
    AAsset* asset = AAssetManager_open(mgr, assetPath.c_str(), AASSET_MODE_STREAMING);
    if (asset == nullptr)
        return false;

    std::filesystem::create_directories(targetPath.parent_path());
    FILE* out = std::fopen(targetPath.string().c_str(), "wb");
    if (out == nullptr)
    {
        AAsset_close(asset);
        return false;
    }

    std::vector<char> buf(64 * 1024);
    int read = 0;
    bool ok = true;
    while ((read = AAsset_read(asset, buf.data(), buf.size())) > 0)
    {
        if (std::fwrite(buf.data(), 1, static_cast<size_t>(read), out) != static_cast<size_t>(read))
        {
            ok = false;
            break;
        }
    }
    if (read < 0)
        ok = false;

    std::fclose(out);
    AAsset_close(asset);
    return ok;
}

// 拷贝 APK 内一个 asset 目录（apkDir 为相对 APK assets 根的目录，"" 表示根）到 targetDir
void CopyAssetDir(AAssetManager* mgr, const char* apkDir, const std::filesystem::path& targetDir)
{
    AAssetDir* dir = AAssetManager_openDir(mgr, apkDir);
    if (dir == nullptr)
        return;

    const std::string prefix = (apkDir[0] == '\0') ? std::string() : std::string(apkDir) + "/";
    uint32_t copied = 0;
    while (const char* name = AAssetDir_getNextFileName(dir))
    {
        const std::string assetPath = prefix + name;
        if (CopyAssetFile(mgr, assetPath, targetDir / name))
            ++copied;
    }
    AAssetDir_close(dir);
    if (copied > 0)
        BH_ANDROID_LOG("APK assets: %s -> %u file(s)", assetDirLabel(apkDir), copied);
}
} // namespace

void PrepareAndroidAssets(struct android_app* app)
{
    AAssetManager* mgr = app->activity->assetManager;
    if (mgr == nullptr)
    {
        BH_ANDROID_LOG("AAssetManager 不可用，跳过资源落地");
        return;
    }
    const std::filesystem::path internal = app->activity->internalDataPath;
    if (internal.empty())
    {
        BH_ANDROID_LOG("internalDataPath 为空，跳过资源落地");
        return;
    }

    // 引擎资源目录约定：<cwd>/assets/... 与 <cwd>/shaders/*.spv
    // APK 内布局：assets 根 = 引擎 assets/ 内容 + shaders/*.spv（由 CMake POST_BUILD 同步）
    // 已知子目录清单：AAssetDir 枚举不返回子目录，按引擎实际目录结构显式列出
    const std::filesystem::path assetsDir = internal / "assets";
    CopyAssetDir(mgr, "", assetsDir); // 顶层（tiles.png 等）
    CopyAssetDir(mgr, "models", assetsDir / "models");
    CopyAssetDir(mgr, "audio", assetsDir / "audio");
    CopyAssetDir(mgr, "fonts", assetsDir / "fonts");
    CopyAssetDir(mgr, "shaders", internal / "shaders");

    // 工作目录切到内部存储：引擎全部相对路径（assets/...、shaders/...）零改动生效
    if (::chdir(internal.string().c_str()) != 0)
        BH_ANDROID_LOG("chdir(%s) 失败", internal.string().c_str());
    else
        BH_ANDROID_LOG("工作目录: %s", internal.string().c_str());
}
} // namespace BigHero
