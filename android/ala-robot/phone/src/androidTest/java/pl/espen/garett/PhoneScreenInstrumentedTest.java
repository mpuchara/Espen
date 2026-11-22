package pl.espen.garett;

import static org.junit.Assert.*;

import android.content.Context;
import android.graphics.Bitmap;
import android.graphics.Insets;
import android.os.Build;
import android.os.SystemClock;
import android.view.View;
import android.view.ViewGroup;
import android.view.WindowInsets;
import android.widget.ScrollView;
import androidx.test.core.app.ActivityScenario;
import androidx.test.ext.junit.runners.AndroidJUnit4;
import androidx.test.platform.app.InstrumentationRegistry;
import java.io.File;
import java.io.FileOutputStream;
import org.junit.Test;
import org.junit.runner.RunWith;

@RunWith(AndroidJUnit4.class)
public final class PhoneScreenInstrumentedTest {
    @Test public void nativeLayoutKeepsFaceAndDockOutsideScrollingChat() throws Exception {
        Context context=InstrumentationRegistry.getInstrumentation().getTargetContext();
        context.getSharedPreferences("espen_watch_v1",0).edit()
            .putString("ota","https://127.0.0.1:1/ota/").commit();
        try (ActivityScenario<WatchActivity> app=ActivityScenario.launch(WatchActivity.class)) {
            app.onActivity(view->{
                PhoneScreen root=screen(view);
                View face=root.findViewById(0x23010001);
                ScrollView chat=root.findViewById(0x23010002);
                View dock=root.findViewById(0x23010003);
                assertTrue(face.getHeight()>root.getHeight()*.36f);
                assertTrue(face.getHeight()<root.getHeight()*.52f);
                assertTrue(dock.getTop()>face.getBottom());
                assertSame(root,dock.getParent());
                assertNotSame(chat,dock.getParent());
                root.message(10,true,"Co wydarzyło się w magicznym lesie?");
                root.message(10,false,"Zza drzew wyjrzał smok Iskra.");
                root.message(10,false,"Ku zaskoczeniu wszystkich, zaprosił nas na przygodę!");
                root.message(11,true,"A co było dalej?");
                view.state(AlaBehavior.Phase.THINKING);
                view.status("Ala przygotowuje opowieść…");
            });
            SystemClock.sleep(850);
            Bitmap image=InstrumentationRegistry.getInstrumentation().getUiAutomation().takeScreenshot();
            assertNotNull(image);
            File directory=context.getExternalFilesDir(null);
            if (directory!=null) try (FileOutputStream saved=new FileOutputStream(new File(directory,"ala-031-thinking.png"))) {
                image.compress(Bitmap.CompressFormat.PNG,100,saved);
            }
            image.recycle();

            app.onActivity(view->{
                PhoneScreen root=screen(view);
                for (int row=0;row<35;row++) {
                    root.message(20+row,row%2==0,"Duża opowieść z polskimi znakami: źdźbło, gęślą, żółć. Fragment "+row);
                }
                root.controls(false,false,false,false,false);
                ViewGroup dock=root.findViewById(0x23010003);
                for (int i=0;i<3;i++) assertFalse(dock.getChildAt(i).isEnabled());
            });
            SystemClock.sleep(350);
            app.onActivity(view->{
                PhoneScreen root=screen(view);
                ScrollView chat=root.findViewById(0x23010002);
                assertTrue("Chat should follow the latest bubble",chat.getScrollY()>0);
                root.controls(true,true,true,true,true);
                ViewGroup dock=root.findViewById(0x23010003);
                for (int i=0;i<3;i++) assertTrue(dock.getChildAt(i).isEnabled());
                if (Build.VERSION.SDK_INT>=30) {
                    int ime=root.getHeight()/3;
                    root.dispatchApplyWindowInsets(new WindowInsets.Builder()
                        .setInsets(WindowInsets.Type.ime(),Insets.of(0,0,0,ime))
                        .setVisible(WindowInsets.Type.ime(),true).build());
                    assertEquals(ime,root.getPaddingBottom());
                    root.measure(View.MeasureSpec.makeMeasureSpec(root.getWidth(),View.MeasureSpec.EXACTLY),
                        View.MeasureSpec.makeMeasureSpec(root.getHeight(),View.MeasureSpec.EXACTLY));
                    assertTrue(root.findViewById(0x23010001).getMeasuredHeight()>0);
                }
            });
        } finally {
            context.getSharedPreferences("espen_watch_v1",0).edit().remove("ota").commit();
        }
    }

    private static PhoneScreen screen(WatchActivity activity){
        ViewGroup content=activity.findViewById(android.R.id.content);
        return (PhoneScreen)content.getChildAt(0);
    }
}
