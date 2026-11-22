package pl.espen.garett;

import static org.junit.Assert.*;

import android.Manifest;
import android.content.Context;
import android.os.SystemClock;
import androidx.test.core.app.ActivityScenario;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;
import java.io.FileInputStream;
import io.github.jaredmdobson.concentus.OpusApplication;
import io.github.jaredmdobson.concentus.OpusEncoder;
import java.util.Arrays;
import java.util.concurrent.BlockingQueue;
import java.util.concurrent.CountDownLatch;
import java.util.concurrent.LinkedBlockingQueue;
import java.util.concurrent.TimeUnit;
import okhttp3.OkHttpClient;
import okhttp3.WebSocket;
import okhttp3.WebSocketListener;
import okhttp3.mockwebserver.MockResponse;
import okhttp3.mockwebserver.MockWebServer;
import okhttp3.tls.HandshakeCertificates;
import okhttp3.tls.HeldCertificate;
import okio.ByteString;
import org.json.JSONObject;
import org.junit.After;
import org.junit.Before;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(AndroidJUnit4.class)
public class XiaozhiSessionInstrumentedTest {
    private static final String HELLO="{\"type\":\"hello\",\"transport\":\"websocket\",\"session_id\":\"test\",\"audio_params\":{\"sample_rate\":16000}}";
    private final BlockingQueue<String> results=new LinkedBlockingQueue<>();
    private final BlockingQueue<AlaBehavior.Phase> states=new LinkedBlockingQueue<>();
    private final BlockingQueue<String> sentences=new LinkedBlockingQueue<>();
    private MockWebServer backend;
    private ActivityScenario<WatchActivity> activity;
    private XiaozhiClient client;
    private Context context;
    private OkHttpClient transport;
    private HandshakeCertificates trust;
    private String latestStatus="";

    @Before public void setup() throws Exception {
        HeldCertificate certificate=new HeldCertificate.Builder().addSubjectAlternativeName("localhost").build();
        HandshakeCertificates serverKeys=new HandshakeCertificates.Builder().heldCertificate(certificate).build();
        trust=new HandshakeCertificates.Builder().addTrustedCertificate(certificate.certificate()).build();
        backend=new MockWebServer();
        backend.useHttps(serverKeys.sslSocketFactory(),false);
        backend.start();
        context=InstrumentationRegistry.getInstrumentation().getTargetContext();
        context.getSharedPreferences("espen_watch_v1",0).edit().putString("ota","https://127.0.0.1:1/ota/").commit();
        activity=ActivityScenario.launch(WatchActivity.class);
        context.getSharedPreferences("espen_watch_v1",0).edit().putString("ota",backend.url("/ota/").toString()).commit();
        transport=new OkHttpClient.Builder().sslSocketFactory(trust.sslSocketFactory(),trust.trustManager()).build();
        activity.onActivity(view->client=new XiaozhiClient(view,new XiaozhiClient.Events(){
            public void state(AlaBehavior.Phase phase){
                states.add(phase);
                if (phase==AlaBehavior.Phase.ERROR) results.add("error");
            }
            public void status(String message){latestStatus=message;}
            public void caption(String message){}
            public void emotion(String message){}
            public void level(int value,boolean microphone){}
            public void activation(String code){}
            public void playedSentence(String text){sentences.add(text);}
            public void userIdea(String text){results.add(text);}
            public void turnFinished(){results.add("complete");}
        },transport));
        backend.enqueue(new MockResponse().setBody(new JSONObject().put("websocket",new JSONObject()
            .put("url",backend.url("/ws").toString().replace("https:","wss:")).put("version",3)).toString()));
    }

    @After public void teardown() throws Exception {
        if (activity!=null) {
            if (client!=null) activity.onActivity(view->client.shutdown());
            activity.close();
        }
        if (backend!=null) backend.shutdown();
        if (context!=null) context.getSharedPreferences("espen_watch_v1",0).edit().remove("ota").remove("vision").commit();
    }

    @Test public void tenSuccessiveNarrationTurnsPlayActualOpus() throws Exception {
        ByteString pcm=opus();
        for (int i=0;i<10;i++) {
            Peer peer=new Peer(){
                @Override void stopped(WebSocket ws){reply(ws,pcm);}
            };
            enqueue(peer);
            waitIdle();
            activity.onActivity(view->assertTrue(client.playButton("lake","")));
            assertEquals("complete",results.poll(25,TimeUnit.SECONDS));
            assertEquals("Ala mówi.",sentences.poll(1,TimeUnit.SECONDS));
            assertTrue(peer.frames>=20);
        }
    }

    @Test public void contextIsReturnedBeforeTheAudibleChapter() throws Exception {
        ByteString pcm=opus();
        enqueue(new Peer(){
            @Override void stopped(WebSocket ws){
                ws.send("{\"type\":\"tts\",\"state\":\"start\"}");
                ws.send("{\"type\":\"tts\",\"state\":\"sentence_start\",\"text\":\"Wstęp narzędzia\"}");
                ws.send(pcm);
                ws.send("{\"type\":\"mcp\",\"payload\":{\"jsonrpc\":\"2.0\",\"method\":\"tools/call\",\"id\":19,\"params\":{\"name\":\"self.story.read_context\"}}}");
            }
            @Override void other(WebSocket ws,JSONObject message){
                if (message.optString("type").equals("mcp") && message.toString().contains("Dobry smok Iskra"))
                    reply(ws,pcm);
            }
        });
        activity.onActivity(view->assertTrue(client.playButton("story_context","Dobry smok Iskra")));
        assertEquals("complete",results.poll(25,TimeUnit.SECONDS));
        assertEquals("Ala mówi.",sentences.poll(1,TimeUnit.SECONDS));
        assertNull(sentences.poll());
    }

    @Test public void responseWithNoAudioFailsAndNextTurnRecovers() throws Exception {
        enqueue(new Peer(){
            @Override void stopped(WebSocket ws){
                ws.send("{\"type\":\"tts\",\"state\":\"start\"}");
                ws.send("{\"type\":\"tts\",\"state\":\"stop\"}");
            }
        });
        activity.onActivity(view->assertTrue(client.playButton("lake","")));
        assertEquals("error",results.poll(24,TimeUnit.SECONDS));
        ByteString pcm=opus();
        enqueue(new Peer(){ @Override void stopped(WebSocket ws){reply(ws,pcm);} });
        activity.onActivity(view->assertTrue(client.playButton("lake","")));
        assertEquals("complete",results.poll(24,TimeUnit.SECONDS));
    }

    @Test public void missingHelloTimesOutAndDisconnectIsReported() throws Exception {
        enqueue(new Peer(){ @Override void hello(WebSocket ws){} });
        activity.onActivity(view->assertTrue(client.playButton("lake","")));
        assertEquals("error",results.poll(18,TimeUnit.SECONDS));
        enqueue(new Peer(){ @Override void started(WebSocket ws){ws.close(1000,"test");} });
        activity.onActivity(view->assertTrue(client.playButton("lake","")));
        assertEquals("error",results.poll(5,TimeUnit.SECONDS));
    }

    @Test public void foregroundExitCancelsStartedTurnAndResumeCanOpenAnother() throws Exception {
        Peer stopped=new Peer();
        enqueue(stopped);
        activity.onActivity(view->assertTrue(client.playButton("lake","")));
        assertTrue(stopped.listen.await(5,TimeUnit.SECONDS));
        activity.onActivity(view->client.foreground(false));
        waitIdle();
        assertNull(results.poll(1,TimeUnit.SECONDS));
        ByteString pcm=opus();
        enqueue(new Peer(){ @Override void stopped(WebSocket ws){reply(ws,pcm);} });
        activity.onActivity(view->{
            client.foreground(true);
            assertTrue(client.playButton("lake",""));
        });
        assertEquals("complete",results.poll(24,TimeUnit.SECONDS));
    }

    @Test public void ownIdeaStopsBeforeCloudTtsAndReturnsStt() throws Exception {
        grant(Manifest.permission.RECORD_AUDIO);
        assertEquals(android.content.pm.PackageManager.PERMISSION_GRANTED,
            context.checkSelfPermission(Manifest.permission.RECORD_AUDIO));
        CountDownLatch encoded=new CountDownLatch(3);
        enqueue(new Peer(){
            @Override public void onMessage(WebSocket ws,ByteString packet){
                if (packet.size()>4) encoded.countDown();
            }
            @Override void stopped(WebSocket ws){
                ws.send("{\"type\":\"stt\",\"text\":\"Niech smok pomoże dzieciom\"}");
                ws.send("{\"type\":\"tts\",\"state\":\"start\"}");
            }
        });
        activity.onActivity(view->client.captureIdea());
        assertTrue("AudioRecord nie wysłał trzech ramek Opus",encoded.await(14,TimeUnit.SECONDS));
        activity.onActivity(view->client.toggle());
        assertEquals("Niech smok pomoże dzieciom",results.poll(10,TimeUnit.SECONDS));
        assertFalse(states.contains(AlaBehavior.Phase.SPEAKING));
    }

    @Test public void cameraConsentStagesOneImageAndUsesMcpVisionBeforeSpeaking() throws Exception {
        grant(Manifest.permission.CAMERA);
        ByteString pcm=opus();
        enqueue(new Peer(){
            @Override void hello(WebSocket ws){
                ws.send(initializes(backend.url("/vision").toString()));
                super.hello(ws);
            }
            @Override void stopped(WebSocket ws){
                ws.send("{\"type\":\"mcp\",\"payload\":{\"jsonrpc\":\"2.0\",\"method\":\"tools/call\",\"id\":5,"
                    +"\"params\":{\"name\":\"self.camera.take_photo\"}}}");
            }
            @Override void other(WebSocket ws,JSONObject message){
                if (message.toString().contains("Symulowane zdjęcie")) reply(ws,pcm);
            }
        });
        backend.enqueue(new MockResponse().setBody("{\"success\":true,\"data\":{\"text\":\"Symulowane zdjęcie\"}}"));
        activity.onActivity(view->{
            client.setVisionEnabled(false);
            client.previewCamera();
            assertFalse(client.busy());
            client.setVisionEnabled(true);
            client.previewCamera();
        });
        assertEquals("complete",results.poll(28,TimeUnit.SECONDS));
        assertEquals("/ota/",backend.takeRequest(2,TimeUnit.SECONDS).getPath());
        assertEquals("/ws",backend.takeRequest(2,TimeUnit.SECONDS).getPath());
        okhttp3.mockwebserver.RecordedRequest upload=backend.takeRequest(2,TimeUnit.SECONDS);
        assertNotNull(upload);
        assertEquals("/vision",upload.getPath());
        assertTrue(upload.getBodySize()>500);
        assertEquals(3,backend.getRequestCount());
        assertTrue(states.contains(AlaBehavior.Phase.CAPTURING));
        assertTrue(states.contains(AlaBehavior.Phase.VISION_ANALYZING));
        assertTrue(states.contains(AlaBehavior.Phase.SPEAKING));
        activity.onActivity(view->client.setVisionEnabled(false));
    }

    @Test public void legacyEndpointFailureEndsTurnBeforeSendingVoiceCommand() throws Exception {
        grant(Manifest.permission.CAMERA);
        Peer peer=new Peer(){
            @Override void hello(WebSocket ws){
                super.hello(ws);
                ws.send(initializes(backend.url("/vision").toString()));
            }
        };
        enqueue(peer);
        backend.enqueue(new MockResponse().setResponseCode(503));
        activity.onActivity(view->{
            client.setVisionEnabled(true);
            client.previewCamera();
        });
        assertEquals("error",results.poll(20,TimeUnit.SECONDS));
        assertTrue(latestStatus,latestStatus.contains("HTTP 503"));
        assertEquals(0,peer.frames);
        assertFalse(client.busy());
        assertFalse(states.contains(AlaBehavior.Phase.SPEAKING));
    }

    @Test public void cameraToolWithoutEndpointReportsSpecificError() throws Exception {
        grant(Manifest.permission.CAMERA);
        enqueue(new Peer(){
            @Override void stopped(WebSocket ws){ws.send(cameraCall());}
        });
        activity.onActivity(view->{
            client.setVisionEnabled(true);
            client.previewCamera();
        });
        assertEquals("error",results.poll(25,TimeUnit.SECONDS));
        assertTrue(latestStatus,latestStatus.contains("bezpiecznego adresu Vision"));
        assertEquals(2,backend.getRequestCount());
        assertFalse(client.busy());
    }

    @Test public void visionResponsesRejectErrorsAndMalformedBodies() throws Exception {
        assertEquals("Czerwony samochód",Vision.description("{\"success\":true,\"result\":[{\"text\":\"Czerwony samochód\"}]}"));
        assertEquals("Niebieski miś",Vision.description("\"Niebieski miś\""));
        assertThrows(java.io.IOException.class,()->Vision.description("{\"success\":false,\"text\":\"błąd\"}"));
        assertThrows(java.io.IOException.class,()->Vision.description("{\"description\":"));
        assertThrows(java.io.IOException.class,()->Vision.description("<html>Błąd serwera</html>"));
    }

    @Test public void allActivityHandlersIgnoreRapidTouchesUntilStoryAudioDrains() throws Exception {
        ByteString pcm=opus();
        BlockingQueue<WebSocket> stopped=new LinkedBlockingQueue<>();
        enqueue(new Peer(){
            @Override void stopped(WebSocket ws){stopped.add(ws);}
        });
        AlaGameSession[] game={null};
        activity.onActivity(view->{
            try {
                java.lang.reflect.Field clientField=WatchActivity.class.getDeclaredField("xiaozhi");
                clientField.setAccessible(true);
                ((XiaozhiClient)clientField.get(view)).shutdown();
                client.shutdown();
                client=new XiaozhiClient(view,view,new OkHttpClient.Builder()
                    .sslSocketFactory(trust.sslSocketFactory(),trust.trustManager()).build());
                clientField.set(view,client);
                java.lang.reflect.Field gameField=WatchActivity.class.getDeclaredField("games");
                gameField.setAccessible(true);
                game[0]=(AlaGameSession)gameField.get(view);
            } catch (ReflectiveOperationException error) { throw new AssertionError(error); }
            view.onGames();
            view.onChoice(2);
            assertTrue(game[0].protectedTurn());
            for (int touch=0;touch<20;touch++) {
                view.onChoice(touch%3);
                view.onGames();
                view.onCamera();
                view.onMicrophone();
            }
            assertTrue(game[0].protectedTurn());
        });
        WebSocket intro=stopped.poll(24,TimeUnit.SECONDS);
        assertNotNull(intro);
        assertEquals(2,backend.getRequestCount());
        reply(intro,pcm);
        waitIdle();
        InstrumentationRegistry.getInstrumentation().waitForIdleSync();
        activity.onActivity(view->{
            assertFalse(game[0].protectedTurn());
            assertFalse(game[0].isNarrating());
        });
        enqueue(new Peer(){
            @Override void stopped(WebSocket ws){stopped.add(ws);}
        });
        activity.onActivity(view->{
            view.onChoice(1);
            assertTrue(game[0].isNarrating());
            for (int touch=0;touch<20;touch++) {
                view.onGames();view.onCamera();view.onMicrophone();view.onChoice(touch%3);
            }
            assertTrue(game[0].isNarrating());
        });
        WebSocket chapter=stopped.poll(24,TimeUnit.SECONDS);
        assertNotNull(chapter);
        assertEquals(3,backend.getRequestCount());
        activity.onActivity(WatchActivity::onBackPressed);
        reply(chapter,pcm);
        waitIdle();
        activity.onActivity(view->{
            assertFalse(game[0].isNarrating());
            assertTrue(game[0].isRunning());
        });
    }

    private static String initializes(String endpoint){
        return "{\"type\":\"mcp\",\"payload\":{\"jsonrpc\":\"2.0\",\"method\":\"initialize\",\"id\":4,"
            +"\"params\":{\"capabilities\":{\"vision\":{\"url\":\""+endpoint+"\"}}}}}";
    }

    private static String cameraCall(){
        return "{\"type\":\"mcp\",\"payload\":{\"jsonrpc\":\"2.0\",\"method\":\"tools/call\",\"id\":5,"
            +"\"params\":{\"name\":\"self.camera.take_photo\"}}}";
    }

    private void grant(String permission) throws Exception{
        android.os.ParcelFileDescriptor fd=InstrumentationRegistry.getInstrumentation().getUiAutomation()
            .executeShellCommand("pm grant "+context.getPackageName()+" "+permission);
        try (FileInputStream input=new android.os.ParcelFileDescriptor.AutoCloseInputStream(fd)) {
            while (input.read()!=-1) {}
        }
    }

    private void waitIdle() throws InterruptedException {
        for (int i=0;i<100 && client.busy();i++) Thread.sleep(25);
        assertFalse(client.busy());
    }

    private void enqueue(Peer behavior){backend.enqueue(new MockResponse().withWebSocketUpgrade(behavior));}

    private static ByteString opus() throws Exception {
        OpusEncoder encoder=new OpusEncoder(16000,1,OpusApplication.OPUS_APPLICATION_VOIP);
        short[] samples=new short[960];
        for (int i=0;i<samples.length;i++) samples[i]=(short)(Math.sin(i*.23)*6500);
        byte[] out=new byte[1600];
        int bytes=encoder.encode(samples,0,samples.length,out,0,out.length);
        return WsAudio.encode(Arrays.copyOf(out,bytes),3,SystemClock.elapsedRealtime());
    }

    private static void reply(WebSocket socket,ByteString pcm){
        socket.send("{\"type\":\"tts\",\"state\":\"start\"}");
        socket.send("{\"type\":\"tts\",\"state\":\"sentence_start\",\"text\":\"Ala mówi.\"}");
        for (int i=0;i<7;i++) socket.send(pcm);
        socket.send("{\"type\":\"tts\",\"state\":\"stop\"}");
    }

    private static class Peer extends WebSocketListener {
        final CountDownLatch listen=new CountDownLatch(1);
        volatile int frames;
        void hello(WebSocket ws){ws.send(HELLO);}
        void started(WebSocket ws){}
        void stopped(WebSocket ws){}
        void other(WebSocket ws,JSONObject message){}
        @Override public void onMessage(WebSocket ws,ByteString packet){frames++;}
        @Override public void onMessage(WebSocket ws,String packet){
            try {
                JSONObject message=new JSONObject(packet);
                switch (message.optString("type")) {
                    case "hello": hello(ws); break;
                    case "listen":
                        if (message.optString("state").equals("start")) { listen.countDown(); started(ws); }
                        if (message.optString("state").equals("stop")) stopped(ws);
                        break;
                    default: other(ws,message);
                }
            } catch (Exception error) { ws.close(1011,"test parse"); }
        }
    }
}
