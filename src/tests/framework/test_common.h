#pragma once
// 测试公共头：断言框架 + 各测试模块共用的标准库 / glm 头文件集合。
// 各 test_*.cpp 只需包含本头 + 与自身被测目标相关的引擎头文件即可。

#include "test_assert.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <functional>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

#include <glm/ext/matrix_clip_space.hpp>
#include <glm/ext/matrix_transform.hpp>
#include <glm/glm.hpp>
#include <glm/gtc/quaternion.hpp>
