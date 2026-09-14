#pragma once
// Android APK 资源落地：把 APK 内打包的 assets/ 与 shaders/ 递归拷贝到应用内部存储，
// 并将工作目录切换过去——使引擎全部相对路径（std::ifstream / std::filesystem）零改动生效。
// 仅在 Android 平台编译。

#ifndef __ANDROID__
#error "AndroidAssets 仅限 Android 平台编译"
#endif

#include <android_native_app_glue.h>

namespace BigHero
{
// 拷贝 APK assets/{assets,shaders} 到 internalDataPath 并 chdir(internalDataPath)。
// 幂等：每次启动全量覆盖（资源量小，成本可忽略；避免版本标记的额外复杂度）。
void PrepareAndroidAssets(struct android_app* app);
} // namespace BigHero
