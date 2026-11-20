package pl.espen.garett;

import android.content.Context;
import android.content.SharedPreferences;
import android.util.Log;
import java.io.PrintWriter;
import java.io.StringWriter;

/** Local bounded stack traces; no audio, photos, tokens or conversation content. */
final class CrashNotes {
    private static boolean installed;
    private CrashNotes(){}

    static void record(Context context,String stage,Throwable error){
        try{
            StringWriter writer=new StringWriter(1600);
            error.printStackTrace(new PrintWriter(writer));
            String text="Etap: "+stage+"\n"+writer.toString();
            if(text.length()>4800)text=text.substring(0,4800);
            text=text.replaceAll("(?i)Bearer\\s+[^\\s]+","Bearer [redacted]")
                .replaceAll("(?i)wss?://[^\\s)]+","[wss endpoint]")
                .replaceAll("(?i)(token|password|secret)=[^&\\s]+","$1=[redacted]");
            context.getApplicationContext()
                .getSharedPreferences("espen_crash",Context.MODE_PRIVATE).edit()
                .putLong("time",System.currentTimeMillis())
                .putString("trace",text).commit();
            Log.e("AlaCrash",stage,error);
        }catch(Throwable ignored){}
    }
    static synchronized void install(Context context){
        if(installed)return;installed=true;
        final Context app=context.getApplicationContext();
        final Thread.UncaughtExceptionHandler previous=Thread.getDefaultUncaughtExceptionHandler();
        Thread.setDefaultUncaughtExceptionHandler((thread,error)->{
            record(app,"Unhandled / "+thread.getName(),error);
            if(previous!=null)previous.uncaughtException(thread,error);
            else System.exit(10);
        });
    }
    static String read(Context c){
        SharedPreferences p=c.getSharedPreferences("espen_crash",Context.MODE_PRIVATE);
        String trace=p.getString("trace","");
        if(trace.isEmpty())return "Brak błędów Java. Nagłe wyjście może być spowodowane przez system.";
        return "Zapisano: "+new java.util.Date(p.getLong("time",0)).toString()+"\n"+trace;
    }
}
