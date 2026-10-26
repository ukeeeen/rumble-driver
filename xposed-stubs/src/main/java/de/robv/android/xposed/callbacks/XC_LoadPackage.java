package de.robv.android.xposed.callbacks;

/** 编译桩 */
public class XC_LoadPackage {
    public static class LoadPackageParam {
        public String packageName;
        public String processName;
        public ClassLoader classLoader;
    }
}
