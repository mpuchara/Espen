package pl.espen.garett;

import android.content.Context;
import android.content.res.ColorStateList;
import android.graphics.Canvas;
import android.graphics.ColorFilter;
import android.graphics.Paint;
import android.graphics.PixelFormat;
import android.graphics.Typeface;
import android.graphics.drawable.Drawable;
import android.graphics.drawable.GradientDrawable;
import android.graphics.drawable.RippleDrawable;
import android.os.Build;
import android.view.Gravity;
import android.view.View;
import android.view.WindowInsets;
import android.widget.Button;
import android.widget.FrameLayout;
import android.widget.LinearLayout;
import android.widget.ScrollView;
import android.widget.TextView;

/** The phone uses native layout and chat controls around the shared face renderer. */
final class PhoneScreen extends LinearLayout {
    private static final int BG=0xff080f16, CARD=0xff09141d, BORDER=0xff233d4b;
    private static final int ACCENT=0xff63e6ff, TEXT=0xffddfaff, MUTED=0xff9bb7c4;
    private static final int MAX_MESSAGES=64, MAX_MESSAGE_LENGTH=3000;
    private final FaceView face;
    private final FrameLayout facePanel;
    private final ScrollView scroll;
    private final LinearLayout messages, options;
    private final TextView status, topic, activation;
    private final Button games,camera,mic,settings;
    private final Button[] choices=new Button[3];
    private final float density;
    private int insetTop,insetBottom,lastFaceHeight=-1;
    private boolean optionsEnabled=true;
    private long bubbleTurn=-1;
    private boolean bubbleUser;
    private TextView bubbleText;

    PhoneScreen(Context context,FaceView face,FaceView.Actions actions){
        super(context);
        density=getResources().getDisplayMetrics().density;
        this.face=face;
        setOrientation(VERTICAL);
        setBackgroundColor(BG);
        setPadding(dp(12),0,dp(12),0);

        facePanel=new FrameLayout(context);
        facePanel.setId(0x23010001);
        facePanel.setBackground(panel(CARD,BORDER,24));
        facePanel.setClipToOutline(true);
        addView(facePanel,new LayoutParams(-1,dp(248)));
        facePanel.addView(face,new FrameLayout.LayoutParams(-1,-1));

        settings=new Button(context);
        settings.setText("⚙");
        settings.setTextColor(MUTED);
        settings.setTextSize(21);
        settings.setContentDescription("Ustawienia");
        settings.setBackground(actionBackground(CARD,BORDER));
        settings.setOnClickListener(v->actions.onSettings());
        FrameLayout.LayoutParams gear=new FrameLayout.LayoutParams(dp(48),dp(48),Gravity.TOP|Gravity.END);
        gear.setMargins(0,dp(8),dp(8),0);
        facePanel.addView(settings,gear);

        LinearLayout conversation=new LinearLayout(context);
        conversation.setOrientation(VERTICAL);
        LayoutParams middle=new LayoutParams(-1,0,1);
        middle.topMargin=dp(10);
        addView(conversation,middle);
        status=label(13,MUTED);
        status.setMaxLines(3);
        status.setPadding(dp(7),dp(3),dp(7),dp(5));
        status.setAccessibilityLiveRegion(ACCESSIBILITY_LIVE_REGION_POLITE);
        status("Dotknij mikrofonu, by porozmawiać",false);
        conversation.addView(status,new LayoutParams(-1,-2));

        activation=label(14,ACCENT);
        activation.setGravity(Gravity.CENTER);
        activation.setVisibility(GONE);
        conversation.addView(activation,new LayoutParams(-1,-2));

        scroll=new ScrollView(context);
        scroll.setId(0x23010002);
        scroll.setClipToPadding(false);
        scroll.setFillViewport(true);
        scroll.setBackground(panel(CARD,BORDER,15));
        conversation.addView(scroll,new LayoutParams(-1,0,1));
        messages=new LinearLayout(context);
        messages.setOrientation(VERTICAL);
        messages.setPadding(dp(9),dp(8),dp(9),dp(8));
        scroll.addView(messages,new ScrollView.LayoutParams(-1,-2));

        options=new LinearLayout(context);
        options.setOrientation(VERTICAL);
        options.setVisibility(GONE);
        LayoutParams optionArea=new LayoutParams(-1,-2);
        optionArea.topMargin=dp(7);
        conversation.addView(options,optionArea);
        topic=label(14,TEXT);
        topic.setTypeface(Typeface.DEFAULT_BOLD);
        topic.setMaxLines(2);
        topic.setPadding(dp(7),0,dp(7),dp(3));
        options.addView(topic,new LayoutParams(-1,-2));
        for (int index=0;index<choices.length;index++) {
            Button choice=new Button(context);
            choice.setTextColor(TEXT);
            choice.setTextSize(14);
            choice.setAllCaps(false);
            choice.setMinHeight(dp(44));
            choice.setPadding(dp(9),0,dp(9),0);
            choice.setBackground(actionBackground(0xff153044,BORDER));
            LayoutParams row=new LayoutParams(-1,dp(44));
            row.bottomMargin=dp(4);
            options.addView(choice,row);
            choices[index]=choice;
            final int selected=index;
            choice.setOnClickListener(v->actions.onChoice(selected));
        }

        LinearLayout dock=new LinearLayout(context);
        dock.setId(0x23010003);
        dock.setGravity(Gravity.CENTER);
        dock.setPadding(0,dp(8),0,dp(8));
        addView(dock,new LayoutParams(-1,dp(75)));
        games=action("Gry",0,actions::onGames);
        camera=action("Aparat",1,actions::onCamera);
        mic=action("Mikrofon",2,actions::onMicrophone);
        for (Button button:new Button[]{games,camera,mic}) {
            LayoutParams slot=new LayoutParams(0,-1,1);
            slot.setMargins(dp(3),0,dp(3),0);
            dock.addView(button,slot);
        }

        setOnApplyWindowInsetsListener((v,insets)->{
            int top,bottom,left,right;
            if (Build.VERSION.SDK_INT>=30) {
                android.graphics.Insets bars=insets.getInsets(WindowInsets.Type.systemBars()
                    | WindowInsets.Type.displayCutout() | WindowInsets.Type.ime());
                top=bars.top; bottom=bars.bottom; left=bars.left; right=bars.right;
            } else {
                top=insets.getSystemWindowInsetTop(); bottom=insets.getSystemWindowInsetBottom();
                left=insets.getSystemWindowInsetLeft(); right=insets.getSystemWindowInsetRight();
            }
            insetTop=top;
            insetBottom=bottom;
            setPadding(dp(12)+left,top+dp(8),dp(12)+right,bottom);
            return insets;
        });
        requestApplyInsets();
    }

    @Override protected void onMeasure(int widthMeasureSpec,int heightMeasureSpec){
        int height=MeasureSpec.getSize(heightMeasureSpec)-insetTop-insetBottom;
        int target=Math.max(dp(92),Math.min(Math.round(height*.44f),height-dp(122)));
        if (target!=lastFaceHeight) {
            facePanel.getLayoutParams().height=target;
            lastFaceHeight=target;
        }
        super.onMeasure(widthMeasureSpec,heightMeasureSpec);
    }

    private Button action(String text,int icon,Runnable clicked){
        Button button=new Button(getContext());
        button.setAllCaps(false);
        button.setText(text);
        button.setTextSize(12);
        button.setTypeface(Typeface.DEFAULT_BOLD);
        button.setTextColor(TEXT);
        button.setGravity(Gravity.CENTER);
        button.setMinWidth(0);
        button.setPadding(dp(3),dp(6),dp(3),dp(5));
        DockIcon drawable=new DockIcon(icon);
        drawable.setBounds(0,0,dp(34),dp(23));
        button.setCompoundDrawables(null,drawable,null,null);
        button.setCompoundDrawablePadding(dp(2));
        button.setBackground(actionBackground(0xff143044,0xff345b70));
        button.setOnClickListener(v->clicked.run());
        return button;
    }

    private int dp(float dp){return Math.round(dp*density);}
    private TextView label(int size,int color){
        TextView view=new TextView(getContext());
        view.setTextSize(size);
        view.setTextColor(color);
        return view;
    }
    private GradientDrawable panel(int fill,int stroke,int radius){
        GradientDrawable shape=new GradientDrawable();
        shape.setColor(fill);
        shape.setCornerRadius(dp(radius));
        shape.setStroke(dp(1),stroke);
        return shape;
    }
    private Drawable actionBackground(int fill,int stroke){
        return new RippleDrawable(ColorStateList.valueOf(0x6063e6ff),panel(fill,stroke,14),null);
    }

    void options(String title,String[] labels){
        options.setVisibility(labels==null?GONE:VISIBLE);
        topic.setText(title);
        if (labels!=null) for (int i=0;i<choices.length;i++) {
            choices[i].setVisibility(i<labels.length?VISIBLE:GONE);
            if (i<labels.length) choices[i].setText(labels[i]);
        }
        highlight(-1);
        followLatest();
    }

    void highlight(int index){
        for (int i=0;i<choices.length;i++) {
            choices[i].setBackground(actionBackground(i==index?0xff286278:0xff153044,
                i==index?ACCENT:BORDER));
            choices[i].animate().cancel();
            choices[i].setAlpha(optionsEnabled || i==index?1f:.52f);
            if (i==index) choices[i].animate().alpha(.78f).setDuration(290)
                .withEndAction(()->choices[index].animate().alpha(1f).setDuration(290).start()).start();
        }
    }

    void controls(boolean gameEnabled,boolean cameraEnabled,boolean micEnabled,boolean choose,boolean config){
        setEnabled(games,gameEnabled);
        setEnabled(camera,cameraEnabled);
        setEnabled(mic,micEnabled);
        settings.setEnabled(config);
        optionsEnabled=choose;
        for (Button choice:choices) {
            choice.setEnabled(choose);
            if (!choose && choice.getBackground()!=null) choice.setAlpha(.52f);
            if (choose) choice.setAlpha(1f);
        }
    }

    private static void setEnabled(Button button,boolean enabled){
        button.setEnabled(enabled);
        button.setAlpha(enabled?1f:.48f);
    }

    void phase(AlaBehavior.Phase phase){
        boolean listening=phase==AlaBehavior.Phase.LISTENING;
        mic.setActivated(listening);
        mic.setBackground(actionBackground(listening?0xff1d5768:0xff143044,listening?ACCENT:0xff345b70));
    }

    void status(String text,boolean error){
        status.setText(text);
        status.setTextColor(error?0xffffa18e:MUTED);
    }
    void markError(){status.setTextColor(0xffffa18e);}
    void activation(String code){
        boolean visible=code!=null && !code.isEmpty();
        activation.setVisibility(visible?VISIBLE:GONE);
        if (visible) activation.setText("Kod Xiaozhi  "+code);
    }

    void message(long owner,boolean user,String sentence){
        if (sentence==null || sentence.trim().isEmpty()) return;
        CharSequence prior=bubbleText==null?"":bubbleText.getText();
        if (owner!=bubbleTurn || user!=bubbleUser || prior.length()+sentence.length()>MAX_MESSAGE_LENGTH) {
            LinearLayout card=new LinearLayout(getContext());
            card.setOrientation(VERTICAL);
            card.setBackground(panel(user?0xff17384a:0xff152735,user?0xff2f6378:BORDER,13));
            card.setPadding(dp(11),dp(7),dp(11),dp(8));
            TextView speaker=label(12,user?0xffacf4e5:ACCENT);
            speaker.setTypeface(Typeface.DEFAULT_BOLD);
            speaker.setText(user?"Ty":"Ala");
            card.addView(speaker);
            bubbleText=label(15,TEXT);
            bubbleText.setPadding(0,dp(3),0,0);
            card.addView(bubbleText);
            LayoutParams row=new LayoutParams(-2,-2);
            row.gravity=user?Gravity.END:Gravity.START;
            row.setMargins(user?dp(30):0,0,user?0:dp(30),dp(8));
            messages.addView(card,row);
            if (messages.getChildCount()>MAX_MESSAGES) messages.removeViewAt(0);
            bubbleTurn=owner;
            bubbleUser=user;
            prior="";
        }
        String chunk=sentence.length()>MAX_MESSAGE_LENGTH?sentence.substring(0,MAX_MESSAGE_LENGTH):sentence;
        bubbleText.setText(prior.length()==0?chunk:prior+" "+chunk);
        followLatest();
    }

    private void followLatest(){
        scroll.post(()->scroll.scrollTo(0,Math.max(0,messages.getHeight()-scroll.getHeight())));
    }

    private static final class DockIcon extends Drawable {
        private final Paint paint=new Paint(Paint.ANTI_ALIAS_FLAG);
        private final int type;
        DockIcon(int type){this.type=type;}
        @Override public void draw(Canvas c){
            c.save();
            c.translate(getBounds().left,getBounds().top);
            c.scale(getBounds().width()/36f,getBounds().height()/24f);
            paint.setColor(ACCENT);
            paint.setStyle(Paint.Style.STROKE);
            paint.setStrokeWidth(2.4f);
            paint.setStrokeCap(Paint.Cap.ROUND);
            if (type==0) {
                c.drawRoundRect(2,5,34,21,7,7,paint);
                c.drawLine(7,13,15,13,paint);
                c.drawLine(11,9,11,17,paint);
                c.drawCircle(23,11,1,paint);
                c.drawCircle(28,16,1,paint);
            } else if (type==1) {
                c.drawRoundRect(3,6,33,22,3,3,paint);
                c.drawCircle(18,14,5,paint);
                c.drawLine(8,3,15,3,paint);
            } else {
                c.drawRoundRect(14,2,22,15,4,4,paint);
                c.drawLine(9,12,9,19,paint);
                c.drawLine(27,12,27,19,paint);
                c.drawLine(9,19,27,19,paint);
                c.drawLine(18,19,18,23,paint);
            }
            c.restore();
        }
        @Override public void setAlpha(int alpha){paint.setAlpha(alpha);}
        @Override public void setColorFilter(ColorFilter filter){paint.setColorFilter(filter);}
        @Override public int getOpacity(){return PixelFormat.TRANSLUCENT;}
    }
}
