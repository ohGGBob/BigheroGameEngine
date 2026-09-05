// miniaudio 实现编译单元：MINIAUDIO_IMPLEMENTATION 必须在且仅在一个 .cpp 中定义。
// 用 #pragma warning 屏蔽第三方库在 /W4 下的告警，不影响引擎自身代码的告警级别。
#define MINIAUDIO_IMPLEMENTATION
#pragma warning(push, 0)
#include "miniaudio.h"
#pragma warning(pop)
