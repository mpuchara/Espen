package pl.espen.alacontroller;

import android.os.Handler;
import android.os.Looper;
import org.json.JSONObject;
import java.net.HttpURLConnection;
import java.net.URL;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;

/**
 * Optional adapter for a user-provided, compatible relay.
 * Espen firmware does not expose this HTTP API: never claim direct control.
 */
final class BridgeClient {
    interface Callback { void complete(boolean success,String message); }
    private final ExecutorService worker=Executors.newSingleThreadExecutor();
    private final Handler ui=new Handler(Looper.getMainLooper());
    private String base="",token="";
    void configure(String url,String secret){
        base=url.trim().replaceAll("/+$","");
        token=secret.trim();
    }
    boolean configured(){return !base.isEmpty();}
    static boolean validUrl(String raw){
        try{
            java.net.URI uri=new java.net.URI(raw);
            String scheme=uri.getScheme(),host=uri.getHost();
            if(scheme==null||host==null||uri.getUserInfo()!=null||uri.getFragment()!=null||
                uri.getQuery()!=null)return false;
            if("https".equalsIgnoreCase(scheme))return true;
            if(!"http".equalsIgnoreCase(scheme))return false;
            String[] octets=host.split("\\.");
            if(octets.length!=4)return "localhost".equalsIgnoreCase(host);
            int[] n=new int[4];
            for(int i=0;i<4;i++){n[i]=Integer.parseInt(octets[i]);if(n[i]<0||n[i]>255)return false;}
            return n[0]==10||n[0]==127||(n[0]==192&&n[1]==168)||
                (n[0]==172&&n[1]>=16&&n[1]<=31);
        }catch(Exception e){return false;}
    }
    void test(Callback cb){invoke("GET",null,cb);}
    void apply(String emotion,boolean blush,boolean permanent,int duration,int delay,Callback cb){
        try{
            JSONObject body=new JSONObject().put("emotion",emotion)
                .put("blush",blush).put("permanent",permanent)
                .put("duration_seconds",duration).put("delay_seconds",delay);
            invoke("POST",body,cb);
        }catch(Exception e){cb.complete(false,"Nie można zakodować polecenia");}
    }
    private void invoke(String verb,JSONObject body,Callback cb){
        final String endpoint=base;
        final String apiToken=token;
        if(endpoint.isEmpty()){ui.post(()->cb.complete(false,"Podgląd na telefonie. Brak skonfigurowanego połączenia z Alą."));return;}
        if(!validUrl(endpoint)){ui.post(()->cb.complete(false,"Błędny adres. HTTPS lub prywatny adres LAN HTTP."));return;}
        worker.execute(()->{
            HttpURLConnection conn=null;
            boolean ok=false;String result;
            try{
                URL url=new URL(endpoint+("GET".equals(verb)?"/api/ala/status":"/api/ala/emotion"));
                conn=(HttpURLConnection)url.openConnection();
                conn.setConnectTimeout(4000);conn.setReadTimeout(4000);
                conn.setRequestMethod(verb);conn.setInstanceFollowRedirects(false);
                conn.setRequestProperty("Accept","application/json");
                if(!apiToken.isEmpty())conn.setRequestProperty("X-Ala-Token",apiToken);
                if(body!=null){
                    conn.setDoOutput(true);conn.setRequestProperty("Content-Type","application/json; charset=utf-8");
                    byte[] data=body.toString().getBytes("UTF-8");
                    if(data.length>2048)throw new IllegalArgumentException("Polecenie za długie");
                    try(java.io.OutputStream output=conn.getOutputStream()){output.write(data);}
                }
                int code=conn.getResponseCode();
                ok=code>=200&&code<300;
                result=ok?("GET".equals(verb)?"Serwer sterujący odpowiada":"Polecenie przyjęte przez serwer"):
                    "Serwer zwrócił HTTP "+code+" (Ala mogła nie odebrać polecenia)";
            }catch(Exception e){
                result="Brak połączenia z serwerem: "+e.getClass().getSimpleName();
            }finally{if(conn!=null)conn.disconnect();}
            final boolean success=ok;final String message=result;
            ui.post(()->cb.complete(success,message));
        });
    }
    void close(){worker.shutdownNow();}
}
