package de.robv.android.xposed;

/** 编译桩：运行时由 LSPosed 提供真实实现 */
public class XC_MethodHook {
    public static class MethodHookParam {
        public Object thisObject;
        public Object[] args;
        private Object result = null;
        private Throwable throwable = null;
        public Object getResult() { return result; }
        public void setResult(Object r) { result = r; throwable = null; }
        public Throwable getThrowable() { return throwable; }
        public boolean hasError() { return throwable != null; }
    }
    public static class Unhook { public void unhook() {} }
    protected void beforeHookedMethod(MethodHookParam param) throws Throwable {}
    protected void afterHookedMethod(MethodHookParam param) throws Throwable {}
}
