namespace BigHero.Runtime
{
    /// <summary>
    /// 脚本日志门面：转发到引擎分级日志（时间戳/级别标签由引擎侧统一处理）。
    /// 宿主未初始化时退化为 Console 输出，保证任何阶段调用都不抛异常。
    /// </summary>
    public static class Log
    {
        public static void Info(string message) => NativeApi.LogWrite(1, message);

        public static void Warn(string message) => NativeApi.LogWrite(2, message);

        public static void Error(string message) => NativeApi.LogWrite(3, message);
    }
}
