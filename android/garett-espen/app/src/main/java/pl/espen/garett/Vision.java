package pl.espen.garett;

import android.Manifest;
import android.app.Activity;
import android.content.pm.PackageManager;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.SurfaceTexture;
import android.hardware.Camera;
import android.os.Handler;
import android.os.Looper;
import android.os.SystemClock;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.text.Normalizer;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import okhttp3.MediaType;
import okhttp3.MultipartBody;
import okhttp3.OkHttpClient;
import okhttp3.Request;
import okhttp3.RequestBody;
import okhttp3.Response;

/** Camera bytes never leave RAM except an explicit, authorized HTTPS vision request. */
@SuppressWarnings("deprecation")
public final class Vision {
    public interface Result { void done(String description, String error); }
    private final Activity activity;
    private final OkHttpClient http;
    private final Handler main = new Handler(Looper.getMainLooper());
    private final ExecutorService network = Executors.newSingleThreadExecutor();
    private volatile boolean enabled = false;
    private volatile long consentUntil = 0;
    private volatile long pictureAt = 0;
    private volatile byte[] jpeg;
    public Vision(Activity a, OkHttpClient h) { activity=a;http=h; }
    public void setEnabled(boolean on) { enabled=on; if(!on) cancel(); }
    public boolean isEnabled() { return enabled; }
    public void cancel() { jpeg=null;consentUntil=0; }
    public static boolean isVisionQuestion(String raw) {
        if(raw==null||raw.length()>512)return false;
        String norm=Normalizer.normalize(raw.toLowerCase(Locale.ROOT),Normalizer.Form.NFD)
            .replaceAll("\\p{M}+","").replace('ł','l');
        if(norm.contains("nie rob zdjec")||norm.contains("nie patrz")||norm.contains("nie wysylaj")) return false;
        String[] terms={"co widzisz","spojrz","popatrz","zrob zdjecie","co trzymam",
            "co mam w rece","co to za","jaki to kolor","jaka to flaga","przeczytaj to",
            "co jest napisane","co jest na obrazku","co pokazuje","zobacz co",
            "opisz ten obraz","sfotografuj","policz przedmioty"};
        for(String term:terms) if(norm.contains(term))return true;
        return false;
    }
    public void noteUserSpeech(String speech) {
        // Every user turn invalidates the previous authorization.
        consentUntil=enabled&&isVisionQuestion(speech)?SystemClock.elapsedRealtime()+45000:0;
    }
    /** Explicit camera button: stage a short-lived shot; never upload by itself. */
    public void preview(Result cb) {
        if(!enabled){cb.done(null,"Włącz Zdjęcia dla AI w ustawieniach");return;}
        consentUntil=SystemClock.elapsedRealtime()+45000;
        capture(cb);
    }
    public void analyze(String question,String url,String token,Result cb) {
        if(!enabled||SystemClock.elapsedRealtime()>consentUntil){
            cb.done(null,"Brak zgody na zdjęcie. Powiedz: Ala, co widzisz?");return;
        }
        if(url==null||!url.startsWith("https://")||url.length()>2048){
            cb.done(null,"Serwer Xiaozhi nie udostępnił bezpiecznego HTTPS Vision");return;
        }
        // One-shot authorization even if upload/capture fails.
        consentUntil=0;
        byte[] ready=jpeg;
        if(ready!=null&&SystemClock.elapsedRealtime()-pictureAt<45000) {
            jpeg=null;upload(ready,question,url,token,cb);
        } else {
            capture((s,e)->{
                if(e!=null){cb.done(null,e);return;}
                byte[] latest=jpeg;jpeg=null;
                if(latest==null){cb.done(null,"Nie udało się zrobić zdjęcia");return;}
                upload(latest,question,url,token,cb);
            });
        }
    }
    private void capture(Result cb){
        if(activity.checkSelfPermission(Manifest.permission.CAMERA)!=PackageManager.PERMISSION_GRANTED){
            cb.done(null,"Brak uprawnienia do aparatu");return;
        }
        main.post(()->{
            Camera camera=null;
            SurfaceTexture surface=null;
            try{
                int id=0;
                Camera.CameraInfo info=new Camera.CameraInfo();
                for(int i=0;i<Camera.getNumberOfCameras();i++){
                    Camera.getCameraInfo(i,info);
                    if(info.facing==Camera.CameraInfo.CAMERA_FACING_FRONT){id=i;break;}
                }
                camera=Camera.open(id);
                if(camera==null) throw new IOException("Kamera jest zajęta");
                Camera.Parameters params=camera.getParameters();
                params.setJpegQuality(75);camera.setParameters(params);
                surface=new SurfaceTexture(12);
                camera.setPreviewTexture(surface);
                camera.startPreview();
                final Camera useCamera=camera;final SurfaceTexture useSurface=surface;
                camera.takePicture(null,null,(bytes,c)->{
                    try{
                        Bitmap src=BitmapFactory.decodeByteArray(bytes,0,bytes.length);
                        if(src==null)throw new IOException("Pusty obraz z aparatu");
                        int max=Math.max(src.getWidth(),src.getHeight());
                        float ratio=Math.min(1f,480f/max);
                        Bitmap smaller=ratio<1f?Bitmap.createScaledBitmap(src,
                            Math.max(1,Math.round(src.getWidth()*ratio)),
                            Math.max(1,Math.round(src.getHeight()*ratio)),true):src;
                        ByteArrayOutputStream out=new ByteArrayOutputStream();
                        if(!smaller.compress(Bitmap.CompressFormat.JPEG,75,out)) throw new IOException("JPEG");
                        jpeg=out.toByteArray();pictureAt=SystemClock.elapsedRealtime();
                        if(smaller!=src)smaller.recycle();src.recycle();
                        cb.done("Zrobiono zdjęcie. Możesz zapytać Alę, co widzi.",null);
                    } catch(Exception ex){cb.done(null,"Błąd aparatu: "+ex.getClass().getSimpleName());}
                    finally{
                        try{useCamera.stopPreview();}catch(Exception ignored){}
                        useCamera.release();useSurface.release();
                    }
                });
            } catch(Exception e) {
                if(camera!=null)camera.release();if(surface!=null)surface.release();
                cb.done(null,"Aparat niedostępny: "+e.getClass().getSimpleName());
            }
        });
    }
    private void upload(byte[] image,String question,String url,String token,Result cb){
        network.execute(()->{
            try{
                RequestBody body=new MultipartBody.Builder().setType(MultipartBody.FORM)
                    .addFormDataPart("question",question==null?"Co widzisz?":question.substring(0,Math.min(500,question.length())))
                    .addFormDataPart("file","camera.jpg",RequestBody.create(MediaType.parse("image/jpeg"),image))
                    .build();
                Request.Builder request=new Request.Builder().url(url).post(body)
                    .header("Device-Id",XiaozhiClient.id(activity).deviceId)
                    .header("Client-Id",XiaozhiClient.id(activity).clientId);
                if(token!=null&&!token.isEmpty())request.header("Authorization","Bearer "+token);
                try(Response response=http.newCall(request.build()).execute()){
                    if(!response.isSuccessful())throw new IOException("HTTPS "+response.code());
                    if(response.body()==null||response.body().contentLength()>8192)
                        throw new IOException("Niepoprawna odpowiedź Vision");
                    String description=response.body().string();
                    if(description.length()>8192||description.isEmpty())
                        throw new IOException("Pusta lub za długa odpowiedź");
                    cb.done(description,null);
                }
            }catch(Exception e){cb.done(null,"Analiza niedostępna: "+e.getMessage());}
        });
    }
    public void shutdown(){cancel();network.shutdownNow();}
}
