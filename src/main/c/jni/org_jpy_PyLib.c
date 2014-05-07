/*
 * Copyright (C) 2014 Brockmann Consult GmbH
 *
 * This program is free software; you can redistribute it and/or modify it under the terms of the GNU
 * General Public License as published by the Free Software Foundation; either version 3 of the License,
 * or (at your option) any later version. This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of MERCHANTABILITY or FITNESS FOR A
 * PARTICULAR PURPOSE. See the GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License along with this program; if not, see
 * http://www.gnu.org/licenses/
 */

#include <jni.h>
#include <Python.h>

#include "jpy_module.h"
#include "jpy_diag.h"
#include "jpy_jtype.h"
#include "jpy_jobj.h"
#include "jpy_conv.h"

#include "org_jpy_PyLib.h"
#include "org_jpy_PyLib_Diag.h"

// Note: Native org.jpy.PyLib function definition headers in this file are formatted according to the header
// generated by javah. This makes it easier to follow up changes in the header.

PyObject* PyLib_GetAttributeObject(JNIEnv* jenv, PyObject* pyValue, jstring jName);
PyObject* PyLib_CallAndReturnObject(JNIEnv *jenv, PyObject* pyValue, jboolean isMethodCall, jstring jName, jint argCount, jobjectArray jArgs, jobjectArray jParamClasses);
void PyLib_HandlePythonException(JNIEnv* jenv);
void PyLib_RedirectStdOut(void);

static int JPy_InitThreads = 0;

#define JPy_JNI_DEBUG 1
//#define JPy_JNI_DEBUG 0

#define JPy_GIL_AWARE

#ifdef JPy_GIL_AWARE
    #define JPy_BEGIN_GIL_STATE  { PyGILState_STATE gilState; if (!JPy_InitThreads) {JPy_InitThreads = 1; PyEval_InitThreads(); PyEval_SaveThread(); } gilState = PyGILState_Ensure();
    #define JPy_END_GIL_STATE    PyGILState_Release(gilState); }
#else
    #define JPy_BEGIN_GIL_STATE
    #define JPy_END_GIL_STATE
#endif


/**
 * Called if the JVM loads this module.
 * Will only called if this module's code is linked into a shared library and loaded by a Java VM.
 */
JNIEXPORT jint JNICALL JNI_OnLoad(JavaVM* jvm, void* reserved)
{
    if (JPy_JNI_DEBUG) printf("JNI_OnLoad: enter: jvm=%p, JPy_JVM=%p, JPy_MustDestroyJVM=%d, Py_IsInitialized()=%d\n",
                              jvm, JPy_JVM, JPy_MustDestroyJVM, Py_IsInitialized());

    if (JPy_JVM == NULL) {
        JPy_JVM = jvm;
        JPy_MustDestroyJVM = JNI_FALSE;
    } else if (JPy_JVM == jvm) {
        if (JPy_JNI_DEBUG) printf("JNI_OnLoad: warning: same JVM already running\n");
    } else {
        if (JPy_JNI_DEBUG) printf("JNI_OnLoad: warning: different JVM already running (expect weird things!)\n");
    }

    if (JPy_JNI_DEBUG) printf("JNI_OnLoad: exit: jvm=%p, JPy_JVM=%p, JPy_MustDestroyJVM=%d, Py_IsInitialized()=%d\n",
                              jvm, JPy_JVM, JPy_MustDestroyJVM, Py_IsInitialized());

    if (JPy_JNI_DEBUG) fflush(stdout);

    return JPY_JNI_VERSION;
}


/**
 * Called if the JVM unloads this module.
 * Will only called if this module's code is linked into a shared library and loaded by a Java VM.
 */
JNIEXPORT void JNICALL JNI_OnUnload(JavaVM* jvm, void* reserved)
{
    JPy_DIAG_PRINT(JPy_DIAG_F_ALL, "JNI_OnUnload: enter: jvm=%p, JPy_JVM=%p, JPy_MustDestroyJVM=%d, Py_IsInitialized()=%d\n",
                   jvm, JPy_JVM, JPy_MustDestroyJVM, Py_IsInitialized());

    Py_Finalize();

    if (!JPy_MustDestroyJVM) {
        JPy_ClearGlobalVars(JPy_GetJNIEnv());
        JPy_JVM = NULL;
    }

    JPy_DIAG_PRINT(JPy_DIAG_F_ALL, "JNI_OnUnload: exit: jvm=%p, JPy_JVM=%p, JPy_MustDestroyJVM=%d, Py_IsInitialized()=%d\n",
                   jvm, JPy_JVM, JPy_MustDestroyJVM, Py_IsInitialized());
}



/*
 * Class:     org_jpy_PyLib
 * Method:    isPythonRunning
 * Signature: ()Z
 */
JNIEXPORT jboolean JNICALL Java_org_jpy_PyLib_isPythonRunning
  (JNIEnv* jenv, jclass jLibClass)
{
    int init;
    init = Py_IsInitialized();
    return init && JPy_Module != NULL;
}


/*
 * Class:     org_jpy_PyLib
 * Method:    startPython
 * Signature: ([Ljava/lang/String;)V
 */
JNIEXPORT void JNICALL Java_org_jpy_PyLib_startPython
  (JNIEnv* jenv, jclass jLibClass, jobjectArray options)
{

    JPy_DIAG_PRINT(JPy_DIAG_F_ALL, "PyLib_startPython: entered: jenv=%p, JPy_Module=%p\n", jenv, JPy_Module);


    if (!Py_IsInitialized()) {
        Py_SetProgramName(L"java");
        Py_Initialize();
        PyLib_RedirectStdOut();
    }

    // if JPy_Module is still NULL, then the 'jpy' module has not been imported yet.
    //
    if (JPy_Module == NULL) {
        PyObject* pyModule;

        // We import 'jpy' so that Python can call our PyInit_jpy() which sets up a number of
        // required global variables (including JPy_Module, see above).
        //
        pyModule = PyImport_ImportModule("jpy");
        if (pyModule == NULL) {
            JPy_DIAG_PRINT(JPy_DIAG_F_ALL, "PyLib_startPython: failed to import module 'jpy'\n");
            if (JPy_DiagFlags != 0 && PyErr_Occurred()) {
                PyErr_Print();
            }
            PyLib_HandlePythonException(jenv);
        }
    }

    JPy_DIAG_PRINT(JPy_DIAG_F_ALL, "PyLib_startPython: exiting: jenv=%p, JPy_Module=%p\n", jenv, JPy_Module);
}
  

/*
 * Class:     org_jpy_PyLib
 * Method:    stopPython
 * Signature: ()V
 */
JNIEXPORT void JNICALL Java_org_jpy_PyLib_stopPython
  (JNIEnv* jenv, jclass jLibClass)
{
    JPy_DIAG_PRINT(JPy_DIAG_F_ALL, "Java_org_jpy_PyLib_stopPython: entered: JPy_Module=%p\n", JPy_Module);

    if (Py_IsInitialized()) {
        Py_Finalize();
    }

    JPy_DIAG_PRINT(JPy_DIAG_F_ALL, "Java_org_jpy_PyLib_stopPython: exiting: JPy_Module=%p\n", JPy_Module);
}


/*
 * Class:     org_jpy_PyLib
 * Method:    getPythonVersion
 * Signature: ()Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_org_jpy_PyLib_getPythonVersion
  (JNIEnv* jenv, jclass jLibClass)
{
    const char* version;

    version = Py_GetVersion();
    if (version == NULL) {
        return NULL;
    }

    return (*jenv)->NewStringUTF(jenv, version);
}


/*
 * Class:     org_jpy_python_PyLib
 * Method:    execScript
 * Signature: (Ljava/lang/String;)J
 */
JNIEXPORT jint JNICALL Java_org_jpy_PyLib_execScript
  (JNIEnv* jenv, jclass jLibClass, jstring jScript)
{
    const char* scriptChars;
    int retCode;

    JPy_BEGIN_GIL_STATE

    scriptChars = (*jenv)->GetStringUTFChars(jenv, jScript, NULL);
    JPy_DIAG_PRINT(JPy_DIAG_F_EXEC, "Java_org_jpy_PyLib_execScript: script='%s'\n", scriptChars);
    retCode = PyRun_SimpleString(scriptChars);
    if (retCode < 0) {
        JPy_DIAG_PRINT(JPy_DIAG_F_ALL, "Java_org_jpy_PyLib_execScript: error: PyRun_SimpleString(\"%s\") returned %d\n", scriptChars, retCode);
        // Note that we cannot retrieve last Python exception after a calling PyRun_SimpleString, see documentation of PyRun_SimpleString.
    }
    (*jenv)->ReleaseStringUTFChars(jenv, jScript, scriptChars);

    JPy_END_GIL_STATE

    return retCode;
}


/*
 * Class:     org_jpy_python_PyLib
 * Method:    incRef
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_org_jpy_PyLib_incRef
  (JNIEnv* jenv, jclass jLibClass, jlong objId)
{
    PyObject* pyObject;
    Py_ssize_t refCount;

    pyObject = (PyObject*) objId;

    if (Py_IsInitialized()) {
        JPy_BEGIN_GIL_STATE

        refCount = pyObject->ob_refcnt;
        JPy_DIAG_PRINT(JPy_DIAG_F_MEM, "Java_org_jpy_PyLib_incRef: pyObject=%p, refCount=%d, type=%s\n", pyObject, refCount, Py_TYPE(pyObject)->tp_name);
        Py_INCREF(pyObject);

        JPy_END_GIL_STATE
    } else {
        JPy_DIAG_PRINT(JPy_DIAG_F_ALL, "Java_org_jpy_PyLib_incRef: error: no interpreter: pyObject=%p\n", pyObject);
    }
}

/*
 * Class:     org_jpy_python_PyLib
 * Method:    decRef
 * Signature: (J)V
 */
JNIEXPORT void JNICALL Java_org_jpy_PyLib_decRef
  (JNIEnv* jenv, jclass jLibClass, jlong objId)
{
    PyObject* pyObject;
    Py_ssize_t refCount;

    pyObject = (PyObject*) objId;

    if (Py_IsInitialized()) {
        JPy_BEGIN_GIL_STATE

        refCount = pyObject->ob_refcnt;
        if (refCount <= 0) {
            JPy_DIAG_PRINT(JPy_DIAG_F_ALL, "Java_org_jpy_PyLib_decRef: error: refCount <= 0: pyObject=%p, refCount=%d\n", pyObject, refCount);
        } else {
            JPy_DIAG_PRINT(JPy_DIAG_F_MEM, "Java_org_jpy_PyLib_decRef: pyObject=%p, refCount=%d, type=%s\n", pyObject, refCount, Py_TYPE(pyObject)->tp_name);
            Py_DECREF(pyObject);
        }

        JPy_END_GIL_STATE
    } else {
        JPy_DIAG_PRINT(JPy_DIAG_F_ALL, "Java_org_jpy_PyLib_decRef: error: no interpreter: pyObject=%p\n", pyObject);
    }
}


/*
 * Class:     org_jpy_python_PyLib
 * Method:    getIntValue
 * Signature: (J)I
 */
JNIEXPORT jint JNICALL Java_org_jpy_PyLib_getIntValue
  (JNIEnv* jenv, jclass jLibClass, jlong objId)
{
    PyObject* pyObject;
    jint value;

    JPy_BEGIN_GIL_STATE

    pyObject = (PyObject*) objId;
    value = (jint) PyLong_AsLong(pyObject);

    JPy_END_GIL_STATE

    return value;
}

/*
 * Class:     org_jpy_python_PyLib
 * Method:    getDoubleValue
 * Signature: (J)D
 */
JNIEXPORT jdouble JNICALL Java_org_jpy_PyLib_getDoubleValue
  (JNIEnv* jenv, jclass jLibClass, jlong objId)
{
    PyObject* pyObject;
    jdouble value;

    JPy_BEGIN_GIL_STATE

    pyObject = (PyObject*) objId;
    value = (jdouble) PyFloat_AsDouble(pyObject);

    JPy_END_GIL_STATE

    return value;
}

/*
 * Class:     org_jpy_python_PyLib
 * Method:    getStringValue
 * Signature: (J)Ljava/lang/String;
 */
JNIEXPORT jstring JNICALL Java_org_jpy_PyLib_getStringValue
  (JNIEnv* jenv, jclass jLibClass, jlong objId)
{
    PyObject* pyObject;
    jstring jString;

    JPy_BEGIN_GIL_STATE

    pyObject = (PyObject*) objId;

    if (JPy_AsJString(jenv, pyObject, &jString) < 0) {
        jString = NULL;
        JPy_DIAG_PRINT(JPy_DIAG_F_ALL, "Java_org_jpy_PyLib_getStringValue: error: failed to convert Python object to Java String\n");
        PyLib_HandlePythonException(jenv);
    }

    JPy_END_GIL_STATE

    return jString;
}

/*
 * Class:     org_jpy_python_PyLib
 * Method:    getObjectValue
 * Signature: (J)Ljava/lang/Object;
 */
JNIEXPORT jobject JNICALL Java_org_jpy_PyLib_getObjectValue
  (JNIEnv* jenv, jclass jLibClass, jlong objId)
{
    PyObject* pyObject;
    jobject jObject;

    JPy_BEGIN_GIL_STATE

    pyObject = (PyObject*) objId;

    if (JObj_Check(pyObject)) {
        jObject = ((JPy_JObj*) pyObject)->objectRef;
    } else {
        if (JPy_AsJObject(jenv, pyObject, &jObject) < 0) {
            jObject = NULL;
            JPy_DIAG_PRINT(JPy_DIAG_F_ALL, "Java_org_jpy_PyLib_getObjectValue: error: failed to convert Python object to Java Object\n");
            PyLib_HandlePythonException(jenv);
        }
    }

    JPy_END_GIL_STATE

    return jObject;
}

/*
 * Class:     org_jpy_PyLib
 * Method:    getObjectArrayValue
 * Signature: (JLjava/lang/Class;)[Ljava/lang/Object;
 */
JNIEXPORT jobjectArray JNICALL Java_org_jpy_PyLib_getObjectArrayValue
  (JNIEnv* jenv, jclass jLibClass, jlong objId, jclass itemClassRef)
{
    PyObject* pyObject;
    jobject jObject;

    JPy_BEGIN_GIL_STATE

    pyObject = (PyObject*) objId;

    if (pyObject == Py_None) {
        jObject = NULL;
    } else if (JObj_Check(pyObject)) {
        jObject = ((JPy_JObj*) pyObject)->objectRef;
    } else if (PySequence_Check(pyObject)) {
        PyObject* pyItem;
        jobject jItem;
        jint i, length;

        length = PySequence_Length(pyObject);

        jObject = (*jenv)->NewObjectArray(jenv, length, itemClassRef, NULL);

        for (i = 0; i < length; i++) {
            pyItem = PySequence_GetItem(pyObject, i);
            if (pyItem == NULL) {
                (*jenv)->DeleteLocalRef(jenv, jObject);
                jObject = NULL;
                goto error;
            }
            if (JPy_AsJObject(jenv, pyItem, &jItem) < 0) {
                (*jenv)->DeleteLocalRef(jenv, jObject);
                jObject = NULL;
                JPy_DIAG_PRINT(JPy_DIAG_F_ALL, "Java_org_jpy_PyLib_getObjectArrayValue: error: failed to convert Python item to Java Object\n");
                PyLib_HandlePythonException(jenv);
                goto error;
            }
            Py_XDECREF(pyItem);
            (*jenv)->SetObjectArrayElement(jenv, jObject, i, jItem);
            if ((*jenv)->ExceptionCheck(jenv)) {
                (*jenv)->DeleteLocalRef(jenv, jObject);
                jObject = NULL;
                goto error;
            }
        }
    } else {
        jObject = NULL;
        (*jenv)->ThrowNew(jenv, JPy_RuntimeException_JClass, "python object cannot be converted to Object[]");
    }

error:
    JPy_END_GIL_STATE

    return jObject;
}


/*
 * Class:     org_jpy_python_PyLib
 * Method:    getModule
 * Signature: (Ljava/lang/String;)J
 */
JNIEXPORT jlong JNICALL Java_org_jpy_PyLib_importModule
  (JNIEnv* jenv, jclass jLibClass, jstring jName)
{
    PyObject* pyName;
    PyObject* pyModule;
    const char* nameChars;

    JPy_BEGIN_GIL_STATE

    nameChars = (*jenv)->GetStringUTFChars(jenv, jName, NULL);
    JPy_DIAG_PRINT(JPy_DIAG_F_EXEC, "Java_org_jpy_PyLib_importModule: name='%s'\n", nameChars);
    /* Note: pyName is a new reference */
    pyName = PyUnicode_FromString(nameChars);
    /* Note: pyModule is a new reference */
    pyModule = PyImport_Import(pyName);
    if (pyModule == NULL) {
        PyLib_HandlePythonException(jenv);
    }
    Py_XDECREF(pyName);
    (*jenv)->ReleaseStringUTFChars(jenv, jName, nameChars);

    JPy_END_GIL_STATE

    return (jlong) pyModule;
}




/*
 * Class:     org_jpy_python_PyLib
 * Method:    getAttributeValue
 * Signature: (JLjava/lang/String;)J
 */
JNIEXPORT jlong JNICALL Java_org_jpy_PyLib_getAttributeObject
  (JNIEnv* jenv, jclass jLibClass, jlong objId, jstring jName)
{
    PyObject* pyObject;
    PyObject* pyValue;

    JPy_BEGIN_GIL_STATE

    pyObject = (PyObject*) objId;
    pyValue = PyLib_GetAttributeObject(jenv, pyObject, jName);

    JPy_END_GIL_STATE

    return (jlong) pyValue;
}

/*
 * Class:     org_jpy_python_PyLib
 * Method:    getAttributeValue
 * Signature: (JLjava/lang/String;Ljava/lang/Class;)Ljava/lang/Object;
 */
JNIEXPORT jobject JNICALL Java_org_jpy_PyLib_getAttributeValue
  (JNIEnv* jenv, jclass jLibClass, jlong objId, jstring jName, jclass jValueClass)
{
    PyObject* pyObject;
    PyObject* pyValue;
    jobject jReturnValue;

    JPy_BEGIN_GIL_STATE

    pyObject = (PyObject*) objId;

    pyValue = PyLib_GetAttributeObject(jenv, pyObject, jName);
    if (pyValue == NULL) {
        jReturnValue = NULL;
        goto error;
    }

    if (JPy_AsJObjectWithClass(jenv, pyValue, &jReturnValue, jValueClass) < 0) {
        jReturnValue = NULL;
        JPy_DIAG_PRINT(JPy_DIAG_F_ALL, "Java_org_jpy_PyLib_getAttributeValue: error: failed to convert attribute value\n");
        PyLib_HandlePythonException(jenv);
    }

error:
    JPy_END_GIL_STATE

    return jReturnValue;
}


/*
 * Class:     org_jpy_python_PyLib
 * Method:    setAttributeValue
 * Signature: (JLjava/lang/String;J)V
 */
JNIEXPORT void JNICALL Java_org_jpy_PyLib_setAttributeValue
  (JNIEnv* jenv, jclass jLibClass, jlong objId, jstring jName, jobject jValue, jclass jValueClass)
{
    PyObject* pyObject;
    const char* nameChars;
    PyObject* pyValue;
    JPy_JType* valueType;

    JPy_BEGIN_GIL_STATE

    pyObject = (PyObject*) objId;

    nameChars = (*jenv)->GetStringUTFChars(jenv, jName, NULL);
    JPy_DIAG_PRINT(JPy_DIAG_F_EXEC, "Java_org_jpy_PyLib_setAttributeValue: objId=%p, name='%s', jValue=%p, jValueClass=%p\n", pyObject, nameChars, jValue, jValueClass);

    if (jValueClass != NULL) {
        valueType = JType_GetType(jenv, jValueClass, JNI_FALSE);
    } else {
        valueType = NULL;
    }

    if (valueType != NULL) {
        pyValue = JPy_FromJObjectWithType(jenv, jValue, valueType);
    } else {
        pyValue = JPy_FromJObject(jenv, jValue);
    }

    if (pyValue == NULL) {
        JPy_DIAG_PRINT(JPy_DIAG_F_ALL, "Java_org_jpy_PyLib_setAttributeValue: error: attribute '%s': Java object not convertible\n", nameChars);
        PyLib_HandlePythonException(jenv);
        goto error;
    }

    if (PyObject_SetAttrString(pyObject, nameChars, pyValue) < 0) {
        JPy_DIAG_PRINT(JPy_DIAG_F_ALL, "Java_org_jpy_PyLib_setAttributeValue: error: PyObject_SetAttrString failed on attribute '%s'\n", nameChars);
        PyLib_HandlePythonException(jenv);
        goto error;
    }

error:
    (*jenv)->ReleaseStringUTFChars(jenv, jName, nameChars);

    JPy_END_GIL_STATE
}


/*
 * Class:     org_jpy_python_PyLib
 * Method:    call
 * Signature: (JZLjava/lang/String;I[Ljava/lang/Object;[Ljava/lang/Class;Ljava/lang/Class;)Ljava/lang/Object;
 */
JNIEXPORT jlong JNICALL Java_org_jpy_PyLib_callAndReturnObject
  (JNIEnv *jenv, jclass jLibClass, jlong objId, jboolean isMethodCall, jstring jName, jint argCount, jobjectArray jArgs, jobjectArray jParamClasses)
{
    PyObject* pyObject;
    PyObject* pyReturnValue;

    JPy_BEGIN_GIL_STATE

    pyObject = (PyObject*) objId;
    pyReturnValue = PyLib_CallAndReturnObject(jenv, pyObject, isMethodCall, jName, argCount, jArgs, jParamClasses);

    JPy_END_GIL_STATE

    return (jlong) pyReturnValue;
}


/*
 * Class:     org_jpy_python_PyLib
 * Method:    callAndReturnValue
 * Signature: (JZLjava/lang/String;I[Ljava/lang/Object;[Ljava/lang/Class;Ljava/lang/Class;)Ljava/lang/Object;
 */
JNIEXPORT jobject JNICALL Java_org_jpy_PyLib_callAndReturnValue
  (JNIEnv *jenv, jclass jLibClass, jlong objId, jboolean isMethodCall, jstring jName, jint argCount, jobjectArray jArgs, jobjectArray jParamClasses, jclass jReturnClass)
{
    PyObject* pyObject;
    PyObject* pyReturnValue;
    jobject jReturnValue;

    JPy_BEGIN_GIL_STATE

    pyObject = (PyObject*) objId;

    pyReturnValue = PyLib_CallAndReturnObject(jenv, pyObject, isMethodCall, jName, argCount, jArgs, jParamClasses);
    if (pyReturnValue == NULL) {
        jReturnValue = NULL;
        goto error;
    }

    if (JPy_AsJObjectWithClass(jenv, pyReturnValue, &jReturnValue, jReturnClass) < 0) {
        JPy_DIAG_PRINT(JPy_DIAG_F_ALL, "Java_org_jpy_PyLib_callAndReturnValue: error: failed to convert attribute value\n");
        PyLib_HandlePythonException(jenv);
        Py_DECREF(pyReturnValue);
        jReturnValue = NULL;
        goto error;
    }

error:
    JPy_END_GIL_STATE

    return jReturnValue;
}


/*
 * Class:     org_jpy_python_PyLib
 * Method:    getDiagFlags
 * Signature: ()I
 */
JNIEXPORT jint JNICALL Java_org_jpy_PyLib_00024Diag_getFlags
  (JNIEnv *jenv, jclass classRef)
{
    return JPy_DiagFlags;
}

/*
 * Class:     org_jpy_python_PyLib
 * Method:    setDiagFlags
 * Signature: (I)V
 */
JNIEXPORT void JNICALL Java_org_jpy_PyLib_00024Diag_setFlags
  (JNIEnv *jenv, jclass classRef, jint flags)
{
    JPy_DiagFlags = flags;
}

////////////////////////////////////////////////////////////////////////////////////
// Helpers that also throw Java exceptions


PyObject* PyLib_GetAttributeObject(JNIEnv* jenv, PyObject* pyObject, jstring jName)
{
    PyObject* pyValue;
    const char* nameChars;

    nameChars = (*jenv)->GetStringUTFChars(jenv, jName, NULL);
    JPy_DIAG_PRINT(JPy_DIAG_F_EXEC, "PyLib_GetAttributeObject: objId=%p, name='%s'\n", pyObject, nameChars);
    /* Note: pyValue is a new reference */
    pyValue = PyObject_GetAttrString(pyObject, nameChars);
    if (pyValue == NULL) {
        JPy_DIAG_PRINT(JPy_DIAG_F_ALL, "PyLib_GetAttributeObject: error: attribute not found '%s'\n", nameChars);
        PyLib_HandlePythonException(jenv);
    }
    (*jenv)->ReleaseStringUTFChars(jenv, jName, nameChars);
    return pyValue;
}

PyObject* PyLib_CallAndReturnObject(JNIEnv *jenv, PyObject* pyObject, jboolean isMethodCall, jstring jName, jint argCount, jobjectArray jArgs, jobjectArray jParamClasses)
{
    PyObject* pyCallable;
    PyObject* pyArgs;
    PyObject* pyArg;
    PyObject* pyReturnValue;
    const char* nameChars;
    jint i;
    jobject jArg;
    jclass jParamClass;
    JPy_JType* paramType;

    pyReturnValue = NULL;

    nameChars = (*jenv)->GetStringUTFChars(jenv, jName, NULL);

    JPy_DIAG_PRINT(JPy_DIAG_F_EXEC, "PyLib_CallAndReturnObject: objId=%p, isMethodCall=%d, name='%s', argCount=%d\n", pyObject, isMethodCall, nameChars, argCount);

    pyArgs = NULL;

    // Note: pyCallable is a new reference
    pyCallable = PyObject_GetAttrString(pyObject, nameChars);
    if (pyCallable == NULL) {
        JPy_DIAG_PRINT(JPy_DIAG_F_ALL, "PyLib_CallAndReturnObject: error: function or method not found: '%s'\n", nameChars);
        PyLib_HandlePythonException(jenv);
        goto error;
    }

    if (!PyCallable_Check(pyCallable)) {
        JPy_DIAG_PRINT(JPy_DIAG_F_ALL, "PyLib_CallAndReturnObject: error: object is not callable: '%s'\n", nameChars);
        PyLib_HandlePythonException(jenv);
        goto error;
    }

    pyArgs = PyTuple_New(argCount);
    for (i = 0; i < argCount; i++) {
        jArg = (*jenv)->GetObjectArrayElement(jenv, jArgs, i);

        if (jParamClasses != NULL) {
            jParamClass = (*jenv)->GetObjectArrayElement(jenv, jParamClasses, i);
        } else {
            jParamClass = NULL;
        }

        if (jParamClass != NULL) {
            paramType = JType_GetType(jenv, jParamClass, JNI_FALSE);
            if (paramType == NULL) {
                JPy_DIAG_PRINT(JPy_DIAG_F_ALL, "PyLib_CallAndReturnObject: error: callable '%s': argument %d: failed to retrieve type\n", nameChars, i);
                PyLib_HandlePythonException(jenv);
                goto error;
            }
            pyArg = JPy_FromJObjectWithType(jenv, jArg, paramType);
            (*jenv)->DeleteLocalRef(jenv, jParamClass);
        } else {
            pyArg = JPy_FromJObject(jenv, jArg);
        }

        (*jenv)->DeleteLocalRef(jenv, jArg);

        if (pyArg == NULL) {
            JPy_DIAG_PRINT(JPy_DIAG_F_ALL, "PyLib_CallAndReturnObject: error: callable '%s': argument %d: failed to convert Java into Python object\n", nameChars, i);
            PyLib_HandlePythonException(jenv);
            goto error;
        }

        // pyArg reference stolen here
        PyTuple_SetItem(pyArgs, i, pyArg);
    }

    // Check why: for some reason, we don't need the following code to invoke object methods.
    /*
    if (isMethodCall) {
        PyObject* pyMethod;

        pyMethod = PyMethod_New(pyCallable, pyObject);
        if (pyMethod == NULL) {
            JPy_DIAG_PRINT(JPy_DIAG_F_EXEC, "PyLib_CallAndReturnObject: error: callable '%s': no memory\n", nameChars);
            PyLib_HandlePythonException(jenv);
            goto error;
        }
        Py_DECREF(pyCallable);
        pyCallable = pyMethod;
    }
    */

    pyReturnValue = PyObject_CallObject(pyCallable, argCount > 0 ? pyArgs : NULL);
    if (pyReturnValue == NULL) {
        JPy_DIAG_PRINT(JPy_DIAG_F_ALL, "PyLib_CallAndReturnObject: error: callable '%s': call returned NULL\n", nameChars);
        PyLib_HandlePythonException(jenv);
        goto error;
    }

    Py_INCREF(pyReturnValue);

error:
    (*jenv)->ReleaseStringUTFChars(jenv, jName, nameChars);
    Py_XDECREF(pyCallable);
    Py_XDECREF(pyArgs);

    return pyReturnValue;
}


void PyLib_HandlePythonException(JNIEnv* jenv)
{
    PyObject* pyType;
    PyObject* pyValue;
    PyObject* pyTraceback;

    PyObject* pyTypeStr;
    PyObject* pyValueStr;
    PyObject* pyTracebackStr;

    PyObject* pyTypeUtf8;
    PyObject* pyValueUtf8;
    PyObject* pyTracebackUtf8;

    char* typeChars;
    char* valueChars;
    char* tracebackChars;
    char* javaMessage;

    //jint ret;

    //printf("PyLib_HandlePythonException 0: jenv=%p\n", jenv);

    if (PyErr_Occurred() == NULL) {
        return;
    }

    // todo - The traceback string generated here does not make sense. Convert it to the actual traceback that Python prints to stderr.

    PyErr_Fetch(&pyType, &pyValue, &pyTraceback);
    PyErr_NormalizeException(&pyType, &pyValue, &pyTraceback);

    //printf("PyLib_HandlePythonException 1: %p, %p, %p\n", pyType, pyValue, pyTraceback);

    pyTypeStr = pyType != NULL ? PyObject_Str(pyType) : NULL;
    pyValueStr = pyValue != NULL ? PyObject_Str(pyValue) : NULL;
    pyTracebackStr = pyTraceback != NULL ? PyObject_Str(pyTraceback) : NULL;

    //printf("PyLib_HandlePythonException 2: %p, %p, %p\n", pyTypeStr, pyValueStr, pyTracebackStr);

    pyTypeUtf8 = pyTypeStr != NULL ? PyUnicode_AsEncodedString(pyTypeStr, "utf-8", "replace") : NULL;
    pyValueUtf8 = pyValueStr != NULL ? PyUnicode_AsEncodedString(pyValueStr, "utf-8", "replace") : NULL;
    pyTracebackUtf8 = pyTracebackStr != NULL ? PyUnicode_AsEncodedString(pyTracebackStr, "utf-8", "replace") : NULL;

    //printf("PyLib_HandlePythonException 3: %p, %p, %p\n", pyTypeUtf8, pyValueUtf8, pyTracebackUtf8);

    typeChars = pyTypeUtf8 != NULL ? PyBytes_AsString(pyTypeUtf8) : NULL;
    valueChars = pyValueUtf8 != NULL ? PyBytes_AsString(pyValueUtf8) : NULL;
    tracebackChars = pyTracebackUtf8 != NULL ? PyBytes_AsString(pyTracebackUtf8) : NULL;

    //printf("PyLib_HandlePythonException 4: %s, %s, %s\n", typeChars, valueChars, tracebackChars);

    javaMessage = PyMem_New(char,
                            (typeChars != NULL ? strlen(typeChars) : 8)
                           + (valueChars != NULL ? strlen(valueChars) : 8)
                           + (tracebackChars != NULL ? strlen(tracebackChars) : 8) + 80);
    if (javaMessage != NULL) {
        if (tracebackChars != NULL) {
            sprintf(javaMessage, "Python error: %s: %s\nTraceback: %s", typeChars, valueChars, tracebackChars);
        } else {
            sprintf(javaMessage, "Python error: %s: %s", typeChars, valueChars);
        }
        //printf("PyLib_HandlePythonException 4a: JPy_RuntimeException_JClass=%p, javaMessage=\"%s\"\n", JPy_RuntimeException_JClass, javaMessage);
        //ret =
        (*jenv)->ThrowNew(jenv, JPy_RuntimeException_JClass, javaMessage);
    } else {
        //printf("PyLib_HandlePythonException 4b: JPy_RuntimeException_JClass=%p, valueChars=\"%s\"\n", JPy_RuntimeException_JClass, valueChars);
        //ret =
        (*jenv)->ThrowNew(jenv, JPy_RuntimeException_JClass, valueChars);
    }

    PyMem_Del(javaMessage);

    Py_XDECREF(pyType);
    Py_XDECREF(pyValue);
    Py_XDECREF(pyTraceback);

    Py_XDECREF(pyTypeStr);
    Py_XDECREF(pyValueStr);
    Py_XDECREF(pyTracebackStr);

    Py_XDECREF(pyTypeUtf8);
    Py_XDECREF(pyValueUtf8);
    Py_XDECREF(pyTracebackUtf8);

    //printf("PyLib_HandlePythonException 5: ret=%d\n", ret);

    PyErr_Clear();
}

////////////////////////////////////////////////////////////////////////////////////////////////
// Redirect stdout

static PyObject* JPrint_write(PyObject* self, PyObject* args)
{
    if (stdout != NULL) {
        const char* text;
        if (!PyArg_ParseTuple(args, "s", &text)) {
            return NULL;
        }
        fprintf(stdout, "%s", text);
    }
    return Py_BuildValue("");
}

static PyObject* JPrint_flush(PyObject* self, PyObject* args)
{
    if (stdout != NULL) {
        fflush(stdout);
    }
    return Py_BuildValue("");
}

static PyMethodDef JPrint_Functions[] = {

    {"write",       (PyCFunction) JPrint_write, METH_VARARGS,
                    "Internal function. Used to print to stdout in embedded mode."},

    {"flush",       (PyCFunction) JPrint_flush, METH_VARARGS,
                    "Internal function. Used to flush to stdout in embedded mode."},

    {NULL, NULL, 0, NULL} /*Sentinel*/
};

static struct PyModuleDef JPrint_ModuleDef =
{
    PyModuleDef_HEAD_INIT,
    "jpy_stdout", /* Name of the Python JPy_Module */
    "Used to redirect 'stdout' to the console in embedded mode",  /* Module documentation */
    -1,                 /* Size of per-interpreter state of the JPy_Module, or -1 if the JPy_Module keeps state in global variables. */
    JPrint_Functions,    /* Structure containing global jpy-functions */
    NULL,     // m_reload
    NULL,     // m_traverse
    NULL,     // m_clear
    NULL      // m_free
};

void PyLib_RedirectStdOut(void)
{
    PyObject* module;
    module = PyModule_Create(&JPrint_ModuleDef);
    PySys_SetObject("stdout", module);
    PySys_SetObject("stderr", module);
}


