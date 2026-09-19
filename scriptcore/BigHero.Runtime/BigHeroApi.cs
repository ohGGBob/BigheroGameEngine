namespace BigHero.Runtime
{
    /// <summary>
    /// 引擎与脚本 SDK 的 API 版本契约。
    /// 宿主（C++ CSharpHost）加载本程序集后第一件事就是校验 <see cref="ApiVersion"/>，
    /// 不匹配则拒绝初始化（DESIGN.md 坑 #7：引擎与脚本 SDK 版本强绑定）。
    /// </summary>
    public static class BigHeroApi
    {
        /// <summary>当前 API 版本（第一增量 U1-S1a/b/c = 1）。</summary>
        public const int ApiVersion = 1;
    }
}
