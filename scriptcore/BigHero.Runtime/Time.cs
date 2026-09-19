namespace BigHero.Runtime
{
    /// <summary>
    /// 帧时间（秒）。派发 OnUpdate 前由宿主写入当帧 dt。
    /// 与 Unity 的 Time.deltaTime 对齐；脚本不要缓存它，直接读。
    /// </summary>
    public static class Time
    {
        /// <summary>上一帧间隔（秒）。宿主每帧派发前刷新；未运行脚本时为 0。</summary>
        public static float DeltaTime { get; internal set; }
    }
}
