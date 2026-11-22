package pl.espen.garett;

import android.Manifest;
import android.app.Activity;
import android.content.pm.PackageManager;
import android.graphics.Bitmap;
import android.graphics.BitmapFactory;
import android.graphics.SurfaceTexture;
import android.hardware.Camera;
import android.os.Handler;
import android.os.HandlerThread;
import android.os.SystemClock;
import java.io.ByteArrayOutputStream;
import java.io.IOException;
import java.text.Normalizer;
import java.util.Locale;
import java.util.concurrent.ExecutorService;
import java.util.concurrent.Executors;
import okhttp3.Call;
import okhttp3.HttpUrl;
import okhttp3.MediaType;
import okhttp3.MultipartBody;
import okhttp3.OkHttpClient;
import okhttp3.Request;
import okhttp3.RequestBody;
import okhttp3.Response;
import okio.BufferedSource;

/** A photograph is captured and delivered once per explicit, short-lived authorization. */
@SuppressWarnings("deprecation")
public final class Vision {
    public interface Result { void done(String description,String error); }
    private interface Photo { void done(byte[] image,String error); }
    private static final long LEASE_MS=45_000;
    private static final long MAX_RESPONSE=8192;
    private final Activity activity;
    private final OkHttpClient http;
    private final ExecutorService network=Executors.newSingleThreadExecutor();
    private HandlerThread cameraThread;
    private Handler cameraHandler;
    private Camera camera;
    private SurfaceTexture surface;
    private int cameraLease;
    private int lease;
    private long until;
    private byte[] staged;
    private Call upload;
    private boolean enabled,closed;

    public Vision(Activity activity,OkHttpClient http){
        this.activity=activity;
        this.http=http.newBuilder().followRedirects(false).followSslRedirects(false).build();
    }

    public synchronized boolean isEnabled(){return enabled;}
    synchronized boolean hasStagedPhoto(){
        return enabled && staged!=null && SystemClock.elapsedRealtime()<until;
    }
    public synchronized void setEnabled(boolean on){
        enabled=on;
        if (!on) cancel();
    }

    public synchronized void cancel(){
        int revoked=lease++;
        until=0;
        staged=null;
        if (upload!=null) { upload.cancel(); upload=null; }
        if (cameraHandler!=null) cameraHandler.post(()->releaseCamera(revoked));
    }

    static boolean isVisionQuestion(String raw){
        if (raw==null || raw.length()>512) return false;
        String norm=Normalizer.normalize(raw.toLowerCase(Locale.ROOT),Normalizer.Form.NFD)
            .replaceAll("\\p{M}+","").replace('ł','l');
        if (norm.contains("nie rob zdjec") || norm.contains("nie patrz") || norm.contains("nie wysylaj"))
            return false;
        String[] terms={"co widzisz","spojrz","popatrz","zrob zdjecie","co trzymam",
            "co mam w rece","jaki to kolor","jaka to flaga","przeczytaj to",
            "co jest napisane","co jest na obrazku","co pokazuje","zobacz co",
            "opisz ten obraz","sfotografuj","policz przedmioty"};
        for (String term:terms) if (norm.contains(term)) return true;
        return false;
    }

    synchronized void noteUserSpeech(String speech){
        cancel();
        if (enabled && isVisionQuestion(speech)) until=SystemClock.elapsedRealtime()+LEASE_MS;
    }

    public synchronized void preview(Result callback){
        if (!enabled || closed) { callback.done(null,"Włącz analizę zdjęć w ustawieniach"); return; }
        cancel();
        int ticket=lease;
        until=SystemClock.elapsedRealtime()+LEASE_MS;
        capture(ticket,(image,error)->{
            synchronized (Vision.this) {
                if (!valid(ticket)) return;
                if (error==null) staged=image;
                else until=0;
                callback.done(error==null ? "Zdjęcie gotowe" : null,error);
            }
        });
    }

    synchronized void analyze(String question,String url,String token,Result callback){
        if (!enabled || closed || until<=SystemClock.elapsedRealtime()) {
            callback.done(null,"Brak zgody na zdjęcie. Dotknij aparatu lub zapytaj Alę, co widzi.");
            return;
        }
        long expires=until;
        until=0;
        byte[] ready=staged;
        staged=null;
        HttpUrl endpoint=url==null || url.length()>2048 ? null : HttpUrl.parse(url);
        if (endpoint==null || !endpoint.isHttps() || !endpoint.username().isEmpty() || !endpoint.password().isEmpty()) {
            callback.done(null,"Xiaozhi nie udostępniło bezpiecznego adresu Vision");
            return;
        }
        int ticket=lease;
        if (ready!=null) {
            submitUpload(ticket,expires,ready,question,endpoint,token,callback);
        } else {
            capture(ticket,(image,error)->{
                synchronized (Vision.this) {
                    if (!valid(ticket)) return;
                    if (error!=null) callback.done(null,error);
                    else submitUpload(ticket,expires,image,question,endpoint,token,callback);
                }
            });
        }
    }

    private synchronized boolean valid(int ticket){return !closed && enabled && lease==ticket;}

    private void capture(int ticket,Photo callback){
        if (activity.checkSelfPermission(Manifest.permission.CAMERA)!=PackageManager.PERMISSION_GRANTED) {
            callback.done(null,"Brak uprawnienia do aparatu");
            return;
        }
        if (cameraHandler==null) {
            cameraThread=new HandlerThread("ala-camera");
            cameraThread.start();
            cameraHandler=new Handler(cameraThread.getLooper());
        }
        cameraHandler.post(()->openCamera(ticket,callback));
    }

    private void openCamera(int ticket,Photo callback){
        if (!valid(ticket)) return;
        if (camera!=null) releaseCamera(cameraLease);
        cameraLease=ticket;
        try {
            if (Camera.getNumberOfCameras()==0) throw new IOException("Brak aparatu");
            int id=0;
            int preferred=activity.getPackageName().equals("pl.espen.garett")
                ? Camera.CameraInfo.CAMERA_FACING_FRONT : Camera.CameraInfo.CAMERA_FACING_BACK;
            Camera.CameraInfo info=new Camera.CameraInfo();
            for (int i=0;i<Camera.getNumberOfCameras();i++) {
                Camera.getCameraInfo(i,info);
                if (info.facing==preferred) { id=i; break; }
            }
            camera=Camera.open(id);
            if (camera==null) throw new IOException("Aparat zajęty");
            Camera.Parameters params=camera.getParameters();
            Camera.Size best=null;
            for (Camera.Size size:params.getSupportedPictureSizes()) {
                int pixels=size.width*size.height;
                if (best==null || (pixels<=1280*960
                    ? pixels>best.width*best.height || best.width*best.height>1280*960
                    : pixels<best.width*best.height)) best=size;
            }
            if (best!=null) params.setPictureSize(best.width,best.height);
            Camera.getCameraInfo(id,info);
            params.setRotation(info.orientation);
            params.setJpegQuality(75);
            camera.setParameters(params);
            surface=new SurfaceTexture(12);
            camera.setPreviewTexture(surface);
            camera.startPreview();
            cameraHandler.postDelayed(()->takePhoto(ticket,callback),450);
            cameraHandler.postDelayed(()->{
                if (camera!=null && cameraLease==ticket) {
                    releaseCamera(ticket);
                    if (valid(ticket)) callback.done(null,"Aparat nie zwrócił zdjęcia");
                }
            },3500);
        } catch (Exception error) {
            releaseCamera(ticket);
            if (valid(ticket)) callback.done(null,"Aparat niedostępny: "+error.getClass().getSimpleName());
        }
    }

    private void takePhoto(int ticket,Photo callback){
        if (camera==null || cameraLease!=ticket || !valid(ticket)) return;
        try {
            camera.takePicture(null,null,(bytes,source)->{
                if (source!=camera) return;
                releaseCamera(ticket);
                synchronized (Vision.this) {
                    if (valid(ticket)) network.execute(()->resize(ticket,bytes,callback));
                }
            });
        } catch (RuntimeException error) {
            releaseCamera(ticket);
            if (valid(ticket)) callback.done(null,"Błąd wykonania zdjęcia");
        }
    }

    private void releaseCamera(int ticket){
        if (cameraLease!=ticket) return;
        if (camera!=null) {
            try { camera.release(); } catch (RuntimeException ignored) {}
            camera=null;
        }
        if (surface!=null) { surface.release(); surface=null; }
    }

    private void resize(int ticket,byte[] bytes,Photo callback){
        if (!valid(ticket)) return;
        Bitmap source=null,small=null;
        try {
            if (bytes==null || bytes.length==0 || bytes.length>4_000_000) throw new IOException("Rozmiar JPEG");
            BitmapFactory.Options options=new BitmapFactory.Options();
            options.inJustDecodeBounds=true;
            BitmapFactory.decodeByteArray(bytes,0,bytes.length,options);
            int maximum=Math.max(options.outWidth,options.outHeight);
            if (maximum<=0) throw new IOException("Nieprawidłowy JPEG");
            options.inJustDecodeBounds=false;
            options.inSampleSize=1;
            while (maximum/options.inSampleSize>640) options.inSampleSize*=2;
            options.inPreferredConfig=Bitmap.Config.RGB_565;
            source=BitmapFactory.decodeByteArray(bytes,0,bytes.length,options);
            if (source==null) throw new IOException("Nie można odczytać JPEG");
            int width=source.getWidth(),height=source.getHeight();
            float ratio=Math.min(1f,480f/Math.max(width,height));
            small=ratio<1f ? Bitmap.createScaledBitmap(source,Math.max(1,Math.round(width*ratio)),
                Math.max(1,Math.round(height*ratio)),true) : source;
            ByteArrayOutputStream out=new ByteArrayOutputStream();
            if (!small.compress(Bitmap.CompressFormat.JPEG,72,out)) throw new IOException("Kodowanie JPEG");
            if (valid(ticket)) callback.done(out.toByteArray(),null);
        } catch (Exception error) {
            if (valid(ticket)) callback.done(null,"Nie można przygotować zdjęcia");
        } finally {
            if (small!=null && small!=source) small.recycle();
            if (source!=null) source.recycle();
        }
    }

    private void submitUpload(int ticket,long expires,byte[] image,String question,HttpUrl endpoint,String token,Result callback){
        network.execute(()->{
            Call request=null;
            try {
                RequestBody form=new MultipartBody.Builder().setType(MultipartBody.FORM)
                    .addFormDataPart("question",question==null ? "Co widzisz?" : question.substring(0,Math.min(500,question.length())))
                    .addFormDataPart("file","camera.jpg",RequestBody.create(MediaType.parse("image/jpeg"),image))
                    .build();
                XiaozhiClient.Identity id=XiaozhiClient.id(activity);
                Request.Builder builder=new Request.Builder().url(endpoint).post(form)
                    .header("Device-Id",id.deviceId).header("Client-Id",id.clientId);
                if (token!=null && !token.isEmpty())
                    builder.header("Authorization",token.startsWith("Bearer ") ? token : "Bearer "+token);
                synchronized (this) {
                    if (!valid(ticket)) return;
                    if (SystemClock.elapsedRealtime()>=expires) {
                        callback.done(null,"Wygasła zgoda na zdjęcie");
                        return;
                    }
                    upload=request=http.newCall(builder.build());
                }
                try (Response response=request.execute()) {
                    if (!response.isSuccessful() || response.body()==null) throw new IOException("Vision HTTP");
                    BufferedSource source=response.body().source();
                    if (source.request(MAX_RESPONSE+1)) throw new IOException("Za długa odpowiedź Vision");
                    String description=source.readUtf8().trim();
                    if (description.isEmpty()) throw new IOException("Pusta odpowiedź Vision");
                    if (valid(ticket)) callback.done(description,null);
                }
            } catch (Exception error) {
                if (valid(ticket)) callback.done(null,"Analiza zdjęcia przez Xiaozhi nie powiodła się");
            } finally {
                synchronized (this) { if (upload==request) upload=null; }
            }
        });
    }

    synchronized void shutdown(){
        cancel();
        closed=true;
        network.shutdownNow();
        if (cameraThread!=null) cameraThread.quitSafely();
    }
}
