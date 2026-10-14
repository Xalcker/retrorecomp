/*
 * Lo que el juego recompilado le pide a Java en Android (make apk, retrorecomp/shared/android): elegir la ROM (y la
 * BIOS de Neo Geo) con el selector de archivos del sistema (RecompActivity.pickFile). Solo entra en la compilación
 * para Android (CMakeLists.txt); en los demás sistemas, ask_rom_path está en files.c.
 */
#include <jni.h>
#include <stdio.h>

#include <SDL.h>

#include "../files.h"        /* platform/files.h: no todos los proyectos tienen -Iplatform */

/* El hilo del juego espera a Java (player_common.h no le pide que guarde al irse a segundo plano) */
SDL_atomic_t android_in_java;

int ask_rom_path(const char *title, char *out, size_t size)
{
    JNIEnv *env = SDL_AndroidGetJNIEnv();
    jobject activity = env ? (jobject)SDL_AndroidGetActivity() : NULL;
    if (!activity) return 0;
    int ok = 0;
    jclass cls = (*env)->GetObjectClass(env, activity);
    jmethodID pick = (*env)->GetStaticMethodID(env, cls, "pickFile", "(Ljava/lang/String;)Ljava/lang/String;");
    if (pick) {
        jstring jtitle = (*env)->NewStringUTF(env, title);
        SDL_AtomicSet(&android_in_java, 1);
        jstring path = (jstring)(*env)->CallStaticObjectMethod(env, cls, pick, jtitle);
        SDL_AtomicSet(&android_in_java, 0);
        if (path) {
            const char *s = (*env)->GetStringUTFChars(env, path, NULL);
            if (s) {
                ok = snprintf(out, size, "%s", s) < (int)size;
                (*env)->ReleaseStringUTFChars(env, path, s);
            }
            (*env)->DeleteLocalRef(env, path);
        }
        if (jtitle) (*env)->DeleteLocalRef(env, jtitle);
    }
    if ((*env)->ExceptionCheck(env)) (*env)->ExceptionClear(env);
    (*env)->DeleteLocalRef(env, cls);
    (*env)->DeleteLocalRef(env, activity);
    return ok;
}
