/*
 * La actividad del juego recompilado en Android (make apk, retrorecomp/shared/android): la SDLActivity de SDL2 con
 * lo propio de estos juegos.
 *  - Arranca en pantalla completa (--fullscreen).
 *  - Lo que la APK trae en assets/datos/ (la ROM de make apk ROM=, la BIOS propia de Neo Geo) se copia a la carpeta
 *    de datos del juego (la de SDL_GetPrefPath) si no está, antes de arrancar el juego.
 *  - pickFile: el selector de archivos del sistema (Storage Access Framework) para elegir la ROM. Lo llama
 *    ask_rom_path (android_jni.c) desde el hilo del juego, que espera aquí la respuesta.
 *  - Al cerrar la actividad termina el proceso: el juego guarda su estado en variables estáticas de C que no se
 *    reinician si Android vuelve a usar el proceso para abrirlo otra vez.
 */
package io.github.xalcker.retrorecomp;

import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.Bundle;
import android.provider.OpenableColumns;
import android.util.Log;
import android.widget.Toast;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

import org.libsdl.app.SDLActivity;

public class RecompActivity extends SDLActivity {
    private static final String TAG = "retrorecomp";
    private static final int PICK_FILE = 1;
    private static final Object lock = new Object();
    private static boolean waiting;
    private static String picked;

    @Override
    protected void onCreate(Bundle state) {
        copyData();
        super.onCreate(state);
    }

    @Override
    protected String[] getArguments() {
        return new String[] { "--fullscreen" };
    }

    @Override
    protected void onDestroy() {
        super.onDestroy();     /* espera a que termine el hilo del juego */
        if (isFinishing()) System.exit(0);
    }

    private void copyData() {
        try {
            String[] names = getAssets().list("datos");
            if (names == null) return;
            for (String name : names) {
                File dst = new File(getFilesDir(), name);
                if (dst.exists()) continue;
                try (InputStream in = getAssets().open("datos/" + name)) {
                    copy(in, dst);
                }
            }
        } catch (IOException e) {
            Log.e(TAG, "no se pudieron copiar los datos de la APK", e);
        }
    }

    /* A un temporal y luego se renombra, para que un corte a la mitad no deje el archivo dañado */
    private static void copy(InputStream in, File dst) throws IOException {
        File tmp = new File(dst.getPath() + ".tmp");
        try (OutputStream out = new FileOutputStream(tmp)) {
            byte[] buf = new byte[1 << 16];
            for (int n; (n = in.read(buf)) > 0; ) out.write(buf, 0, n);
        }
        if (!tmp.renameTo(dst)) throw new IOException("no se pudo renombrar " + tmp);
    }

    /* Abre el selector de archivos con el título como aviso y espera. Devuelve la ruta de una copia del archivo
     * elegido (en la caché de la app, con su nombre) o null si se canceló */
    public static String pickFile(final String title) {
        final SDLActivity activity = mSingleton;
        if (activity == null) return null;
        synchronized (lock) {
            waiting = true;
            picked = null;
        }
        activity.runOnUiThread(() -> {
            Toast.makeText(activity, title, Toast.LENGTH_LONG).show();
            Intent intent = new Intent(Intent.ACTION_OPEN_DOCUMENT);
            intent.addCategory(Intent.CATEGORY_OPENABLE);
            intent.setType("*/*");
            try {
                activity.startActivityForResult(intent, PICK_FILE);
            } catch (Exception e) {
                Log.e(TAG, "no hay selector de archivos", e);
                done(null);
            }
        });
        synchronized (lock) {
            while (waiting) {
                try {
                    lock.wait();
                } catch (InterruptedException e) {
                    return null;
                }
            }
            return picked;
        }
    }

    private static void done(String path) {
        synchronized (lock) {
            picked = path;
            waiting = false;
            lock.notifyAll();
        }
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        if (request != PICK_FILE) {
            super.onActivityResult(request, result, data);
            return;
        }
        final Uri uri = result == RESULT_OK && data != null ? data.getData() : null;
        if (uri == null) {
            done(null);
            return;
        }
        /* Una ROM de Neo Geo pesa decenas de MB: se copia fuera del hilo de la interfaz */
        new Thread(() -> done(copyPicked(uri))).start();
    }

    private String copyPicked(Uri uri) {
        File dir = new File(getCacheDir(), "elegido");
        File[] old = dir.listFiles();
        if (old != null) for (File f : old) f.delete();
        dir.mkdirs();
        String name = "archivo";
        try (Cursor c = getContentResolver().query(uri, new String[] { OpenableColumns.DISPLAY_NAME }, null, null, null)) {
            if (c != null && c.moveToFirst() && c.getString(0) != null) name = c.getString(0).replaceAll("[/\\\\]", "_");
        } catch (Exception e) {
            Log.w(TAG, "sin nombre para " + uri, e);
        }
        File dst = new File(dir, name);
        try (InputStream in = getContentResolver().openInputStream(uri)) {
            if (in == null) return null;
            copy(in, dst);
            return dst.getAbsolutePath();
        } catch (Exception e) {
            Log.e(TAG, "no se pudo copiar " + uri, e);
            return null;
        }
    }
}
