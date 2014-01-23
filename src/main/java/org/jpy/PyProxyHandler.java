package org.jpy;

import java.lang.reflect.InvocationHandler;
import java.lang.reflect.Method;

import static org.jpy.PyLib.assertPythonRuns;

/**
 * The {@code InvocationHandler} for used by the proxy instances created by the
 * {@link PyObject#createProxy(Class)} and {@link PyModule#createProxy(Class)} methods.
 *
 * @author Norman Fomferra
 * @since 1.0
 */
class PyProxyHandler implements InvocationHandler {
    private final PyObject pyObject;
    private final PyLib.CallableKind callableKind;

    public PyProxyHandler(PyObject pyObject, PyLib.CallableKind callableKind) {
        if (pyObject == null) {
            throw new NullPointerException("pyObject");
        }
        this.pyObject = pyObject;
        this.callableKind = callableKind;
    }

    @Override
    public Object invoke(Object proxyObject, Method method, Object[] args) throws Throwable {
        //System.out.printf("invoke: %s(%s)\n", method.getName(), Arrays.toString(args));
        assertPythonRuns();
        return PyLib.callAndReturnValue(
                this.pyObject.getPointer(),
                callableKind == PyLib.CallableKind.METHOD,
                method.getName(),
                args != null ? args.length : 0,
                args,
                method.getParameterTypes(),
                method.getReturnType());
    }
}