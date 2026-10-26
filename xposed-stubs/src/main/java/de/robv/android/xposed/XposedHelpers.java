package de.robv.android.xposed;

/** 编译桩：运行时由 LSPosed 提供真实实现 */
public final class XposedHelpers {
    public static XC_MethodHook.Unhook findAndHookMethod(Class<?> clazz, String methodName, Object... parameterTypesAndCallback) {
        throw new RuntimeException("stub");
    }
    public static Object getObjectField(Object obj, String fieldName) { throw new RuntimeException("stub"); }
    public static long getLongField(Object obj, String fieldName) { throw new RuntimeException("stub"); }
    public static int getIntField(Object obj, String fieldName) { throw new RuntimeException("stub"); }
}
