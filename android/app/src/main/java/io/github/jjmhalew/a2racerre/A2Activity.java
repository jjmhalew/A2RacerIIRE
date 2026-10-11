// A2Activity - SDL's activity plus what the game's first start needs on Android (plat_pick_data in src/plat_sdl.c,
// src/datasetup.c): the system's file picker for an ISO image of the CD or a folder with a copy of it, the folder copy,
// and a progress dialog.
// Testing: adb shell am start -n io.github.jjmhalew.a2racerre/.A2Activity -e args "--track 1"
//
// This file is part of A2RacerIIRE, licensed under the GNU GPL v3 or later (see LICENSE).
package io.github.jjmhalew.a2racerre;

import android.app.Activity;
import android.app.AlertDialog;
import android.content.ContentResolver;
import android.content.Intent;
import android.database.Cursor;
import android.net.Uri;
import android.os.ParcelFileDescriptor;
import android.provider.DocumentsContract;
import android.provider.DocumentsContract.Document;
import android.util.Log;

import org.libsdl.app.SDLActivity;

import java.io.File;
import java.io.FileOutputStream;
import java.io.IOException;
import java.io.InputStream;
import java.io.OutputStream;

public class A2Activity extends SDLActivity {
    private static final String TAG = "A2RacerIIRE";
    private static final int REQ_ISO = 0xa201, REQ_DIR = 0xa202;

    private static final Object sLock = new Object();
    private static boolean sPicked;
    private static Uri sPick;
    private static AlertDialog sProgress;

    @Override
    protected String[] getLibraries() {
        return new String[] { "SDL2", "main" };
    }

    @Override
    protected String[] getArguments() {                  // testing: the command line of src/main.c
        String a = getIntent() != null ? getIntent().getStringExtra("args") : null;
        return a == null || a.trim().isEmpty() ? new String[0] : a.trim().split("\\s+");
    }

    // ---- called from the game's thread ------------------------------------------------------------------------------

    /** kind 1: an ISO image, returns a file descriptor the caller closes; kind 2: a folder (the CD or a copy of it):
     *  the files of its "data" folder (or of the folder itself when it is the data folder) are copied into dest,
     *  returns 0. -1 = cancelled, -2 = failed. Blocks until the user is done. */
    public static int pickGameData(int kind, String dest) {
        final Activity a = (Activity) SDLActivity.getContext();
        if (a == null) return -2;
        synchronized (sLock) { sPicked = false; sPick = null; }
        a.runOnUiThread(() -> {
            Intent i;
            if (kind == 1) { i = new Intent(Intent.ACTION_OPEN_DOCUMENT); i.addCategory(Intent.CATEGORY_OPENABLE); i.setType("*/*"); }
            else i = new Intent(Intent.ACTION_OPEN_DOCUMENT_TREE);
            try { a.startActivityForResult(i, kind == 1 ? REQ_ISO : REQ_DIR); }
            catch (Exception e) { Log.e(TAG, "no file picker", e); picked(null); }
        });
        Uri u;
        synchronized (sLock) {
            while (!sPicked) try { sLock.wait(); } catch (InterruptedException e) { return -1; }
            u = sPick;
        }
        if (u == null) return -1;
        try {
            if (kind == 1) {
                ParcelFileDescriptor pfd = a.getContentResolver().openFileDescriptor(u, "r");
                return pfd != null ? pfd.detachFd() : -2;
            }
            ContentResolver cr = a.getContentResolver();
            String root = DocumentsContract.getTreeDocumentId(u);
            String data = findChild(cr, u, root, "data");       // the CD's root: take its data folder
            File out = new File(dest);
            if (!out.isDirectory() && !out.mkdirs()) throw new IOException("cannot create " + out);
            long[] done = { 0, 0 };
            copyFiles(cr, u, data != null ? data : root, out, done);
            progress(null);
            return 0;
        } catch (Exception e) {
            Log.e(TAG, "pickGameData", e);
            progress(null);
            return -2;
        }
    }

    /** a dialog with this text over the game, null closes it */
    public static void progress(final String text) {
        final Activity a = (Activity) SDLActivity.getContext();
        if (a == null) return;
        a.runOnUiThread(() -> {
            if (text == null) { if (sProgress != null) { sProgress.dismiss(); sProgress = null; } return; }
            if (sProgress == null) {
                sProgress = new AlertDialog.Builder(a, android.R.style.Theme_DeviceDefault_Dialog_Alert)
                    .setTitle("A2RacerIIRE").setMessage(text).setCancelable(false).create();
                sProgress.show();
            } else sProgress.setMessage(text);
        });
    }

    private static void picked(Uri u) {
        synchronized (sLock) { sPick = u; sPicked = true; sLock.notifyAll(); }
    }

    @Override
    protected void onActivityResult(int request, int result, Intent data) {
        if (request == REQ_ISO || request == REQ_DIR) { picked(result == RESULT_OK && data != null ? data.getData() : null); return; }
        super.onActivityResult(request, result, data);
    }

    private static final String[] COLS = { Document.COLUMN_DOCUMENT_ID, Document.COLUMN_DISPLAY_NAME, Document.COLUMN_MIME_TYPE };

    private static String findChild(ContentResolver cr, Uri tree, String docId, String name) {
        try (Cursor q = cr.query(DocumentsContract.buildChildDocumentsUriUsingTree(tree, docId), COLS, null, null, null)) {
            if (q != null) while (q.moveToNext())
                if (name.equalsIgnoreCase(q.getString(1)) && Document.MIME_TYPE_DIR.equals(q.getString(2))) return q.getString(0);
        }
        return null;
    }

    private static void copyFiles(ContentResolver cr, Uri tree, String docId, File dest, long[] done) throws IOException {
        try (Cursor q = cr.query(DocumentsContract.buildChildDocumentsUriUsingTree(tree, docId), COLS, null, null, null)) {
            if (q == null) throw new IOException("cannot list " + docId);
            while (q.moveToNext()) {
                if (Document.MIME_TYPE_DIR.equals(q.getString(2))) continue;
                copyFile(cr, tree, q.getString(0), dest, q.getString(1), done);
            }
        }
    }

    private static void copyFile(ContentResolver cr, Uri tree, String id, File dest, String name, long[] done) throws IOException {
        File f = new File(dest, name), part = new File(dest, name + ".part");
        try (InputStream in = cr.openInputStream(DocumentsContract.buildDocumentUriUsingTree(tree, id));
             OutputStream out = new FileOutputStream(part)) {
            if (in == null) throw new IOException("cannot open " + name);
            byte[] buf = new byte[1 << 20];
            for (int n; (n = in.read(buf)) > 0; ) {
                out.write(buf, 0, n);
                done[0] += n;
                if (done[0] - done[1] >= 8 << 20) { done[1] = done[0]; progress("Copying the game files... " + (done[0] >> 20) + " MB"); }
            }
        }
        if (!part.renameTo(f)) throw new IOException("cannot write " + f);
    }
}
