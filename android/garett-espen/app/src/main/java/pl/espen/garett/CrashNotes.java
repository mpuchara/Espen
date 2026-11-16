package pl.espen.garett;

import android.content.Context;
import android.content.SharedPreferences;
import android.util.Log;
import java.io.PrintWriter;
import java.io.StringWriter;

/**
 * Persist only a short Java crash trace in the app's private storage.
 * No audio, photos, Xiaozhi tokens or request payloads are logged.
 */
final class CrashNotes {
    private static boolean installed;
    private CrashNotes(){}
    static synchronized void install(Context context){
        if(installed)return;
        installed=true;
        final Context app=context.getApplicationContext();
        final Thread.UncaughtExceptionHandler previous=Thread.getDefaultUncaughtExceptionHandler();
        Thread.setDefaultUncaughtExceptionHandler((thread,error)->{
            try {
                StringWriter writer=new StringWriter(1600);
                error.printStackTrace(new PrintWriter(writer));
                String text=writer.toString();
                if(text.length()>4500)text=text.substring(0,4500);
                // Remove URL/token values if a library's exception embeds them.
                text=text.replaceAll("(?i)Bearer\\s+[^\\s]+","Bearer [redacted]")
                    .replaceAll("(?i)wss?://[^\\s)]+","[wss endpoint]");
                app.getSharedPreferences("espen_crash",Context.MODE_PRIVATE).edit()
                    .putLong("time",System.currentTimeMillis())
                    .putString("trace",text).commit();
                Log.e("AlaCrash","Crash captured: "+error.getClass().getSimpleName());
            }catch(Throwable ignored){}
            if(previous!=null)previous.uncaughtException(thread,error);
            else System.exit(10);
        });
    }
    static String read(Context c){
        SharedPreferences p=c.getSharedPreferences("espen_crash",Context.MODE_PRIVATE);
        String trace=p.getString("trace","");
        if(trace.isEmpty())return "Brak zapisanych błędów Java. Nagłe wyjście może być zabiciem procesu przez system.";
        return "Zapisano: "+new java.util.Date(p.getLong("time",0)).toString()+"\n"+trace;
    }
}
